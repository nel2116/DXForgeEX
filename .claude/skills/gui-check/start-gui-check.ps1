<#
.SYNOPSIS
	PR の GUI 確認を、新しいワーカーに任せるスクリプト(#195)。

.DESCRIPTION
	GUI 確認を文脈の大きいコーディネーターで行うと、画像と操作のターンで消費が増える。PR の本文の「GUI の確認手順」の節を
	読み、worker-spec.md と連結した仕様で StartWorker.ps1 からワーカーを起動する。実装者のワーカーとは別にする。

	worktree は gui-check-pr-<N> の名前で origin/develop から新しく作り、ワーカーが refs/pull/<N>/head を fetch して、
	PR の head の sha と一致することを確かめてから切り替える(fork の PR でも同じ)。
	このブランチは PR に結び付かないので CleanupMerged.ps1 は消さない。worker_done のあと、worker-release してから
	-Cleanup で消す。

	マウスとキーの操作はデスクトップ全体に作用するので、GUI 確認は同時に 1 つだけにする。別の gui-check-pr-* の worktree が
	残っていれば(確認中か片付けの前)、起動せずに終了コード 4 を返す。

.PARAMETER Pr
	PR の番号。

.PARAMETER Size
	small(既定、--model claude-sonnet-5-5)か normal。手順に従う作業なので既定は小さいモデルにする。

.PARAMETER Run
	Run の ID。省略すると、このコーディネーターに結び付いている Run。

.PARAMETER DryRun
	実行せず、orca の引数と仕様を表示する。

.PARAMETER Cleanup
	確認の worktree(gui-check-pr-<N>)とブランチを消す。起動はしない。

.EXAMPLE
	./.claude/skills/gui-check/start-gui-check.ps1 -Pr 200
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory)][int]$Pr,

	[ValidateSet("small", "normal")]
	[string]$Size = "small",

	[string]$Run,

	[switch]$DryRun,

	[switch]$Cleanup
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..")).Path
$GhRepo = "nel2116/DXForgeEX"
$WorktreeName = "gui-check-pr-$Pr"

# 「GUI の確認手順」の見出しから、同じ深さ以上の次の見出しまで。コードブロックの中の # は見出しとして扱わない。
# 複数あれば連結する。gui-check の節と、末尾の Closes/Refs と Generated の行は含めない
function Get-GuiCheckSteps([string]$Body) {
	$collected = [System.Collections.Generic.List[string]]::new()
	$level = 0
	$fence = $false
	foreach ($line in $Body -split "\r?\n") {
		if ($line -match '^\s*(```|~~~)') {
			$fence = -not $fence
			if ($level -gt 0) { $collected.Add($line) }
			continue
		}
		$heading = if ($fence) { $null } else { [regex]::Match($line, '^(#{1,6})\s+(.*)$') }
		if ($heading -and $heading.Success) {
			$depth = $heading.Groups[1].Length
			if ($heading.Groups[2].Value -match '^GUI\s*の確認手順') {
				if ($level -eq 0) { $level = $depth }
				continue
			}
			if ($level -gt 0 -and $depth -le $level) { $level = 0 }
		}
		if ($level -eq 0) { continue }
		if (-not $fence -and $line -match '^(<!-- gui-check:start -->|(Closes|Refs|Fixes) #\d+|🤖 Generated with)') { $level = 0; continue }
		$collected.Add($line)
	}
	return (($collected -join "`n").Trim())
}

function Get-GuiCheckWorktrees {
	$porcelain = git -C $RepoRoot worktree list --porcelain
	if ($LASTEXITCODE -ne 0) { Write-Host "git worktree list に失敗しました。"; exit 1 }
	return @($porcelain | ForEach-Object { if ($_ -match '^worktree (.+)$') { $Matches[1] } } |
			Where-Object { (Split-Path -Leaf $_) -match '^gui-check-pr-\d+$' })
}

function Invoke-Cleanup {
	$path = Get-GuiCheckWorktrees | Where-Object { (Split-Path -Leaf $_) -eq $WorktreeName } | Select-Object -First 1
	if (-not $path) { Write-Output "ありません: $WorktreeName"; exit 0 }
	if ($DryRun) { Write-Output "orca worktree rm --worktree path:$path --delete-branch"; exit 0 }
	if (-not (Get-Command orca -ErrorAction SilentlyContinue)) { Write-Host "orca が見つかりません。"; exit 1 }
	# 未コミットの変更があれば Orca が断る(ワーカーはコミットしない)。--force は付けない
	& orca worktree rm --worktree "path:$path" --delete-branch
	exit $LASTEXITCODE
}

if ($Cleanup) { Invoke-Cleanup }

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) { Write-Host "gh が見つかりません。"; exit 1 }
$json = gh pr view $Pr --repo $GhRepo --json "url,title,state,body,headRefName,headRefOid,baseRefName"
if ($LASTEXITCODE -ne 0) { Write-Host "PR #$Pr を読めませんでした。"; exit 1 }
$pull = ($json -join "`n") | ConvertFrom-Json
if ($pull.state -ne "OPEN") { Write-Host "PR #$Pr は開いていません(状態: $($pull.state))。"; exit 2 }

$steps = Get-GuiCheckSteps ([string]$pull.body)
if ($steps -notmatch '(?m)^\s*\d+[.)]\s') {
	Write-Host "PR #$Pr の本文に、番号付きの「GUI の確認手順」の節がありません。実装者に書いてもらってください。"
	exit 3
}

$values = @{
	PR       = "$Pr"
	URL      = [string]$pull.url
	TITLE    = [string]$pull.title
	HEAD     = [string]$pull.headRefOid
	HEAD_REF = [string]$pull.headRefName
	BASE_REF = [string]$pull.baseRefName
	STEPS    = $steps
}
$template = [System.IO.File]::ReadAllText((Join-Path $PSScriptRoot "worker-spec.md"), [System.Text.Encoding]::UTF8)
# 1 回の置き換えにする。手順の中に {{...}} があっても展開しない
$spec = [regex]::Replace($template, '\{\{(\w+)\}\}', { param($m) $values[$m.Groups[1].Value] })

if (-not $DryRun) {
	$running = @(Get-GuiCheckWorktrees)
	if ($running.Count -gt 0) {
		Write-Host "GUI 確認の worktree が残っています: $($running -join ', ')"
		Write-Host "GUI の操作はデスクトップ全体に作用するので、同時に 1 つだけにします。worker_done を待ち、worker-release と -Cleanup のあとで起動してください。"
		exit 4
	}
}

$specFile = Join-Path ([System.IO.Path]::GetTempPath()) ("dxforge_gui_check_spec_" + [guid]::NewGuid().ToString("N") + ".md")
try {
	[System.IO.File]::WriteAllText($specFile, $spec, [System.Text.UTF8Encoding]::new($false))
	$arguments = @{ SpecFile = $specFile; Name = $WorktreeName; Size = $Size }
	if ($Run) { $arguments.Run = $Run }
	if ($DryRun) { $arguments.DryRun = $true }
	& (Join-Path $RepoRoot "StartWorker.ps1") @arguments
	$code = $LASTEXITCODE
}
finally {
	Remove-Item -LiteralPath $specFile -ErrorAction SilentlyContinue
}
if ($code -eq 0 -and -not $DryRun) {
	Write-Output "worker_done の要約を待ってください。そのあと worker-release してから、次を実行して worktree を消します:"
	Write-Output "  ./.claude/skills/gui-check/start-gui-check.ps1 -Pr $Pr -Cleanup"
}
exit $code
