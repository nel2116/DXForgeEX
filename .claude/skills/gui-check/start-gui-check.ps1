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
	同時に起動しても両方が一覧を通り抜けないよう、一覧の確認から StartWorker.ps1 の完了までを名前付きの Mutex で保護する。
	取れなければ待たずに同じ終了コード 4 を返す。Mutex はプロセスが終われば OS が解放するので、ロックは残らない。

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
# Local\ はログオンのセッションごと。操作が作用するデスクトップの範囲と同じ。環境変数はテストを本物の起動から切り離すためのもの
$MutexName = if ($env:DXFORGE_GUI_CHECK_MUTEX) { $env:DXFORGE_GUI_CHECK_MUTEX } else { "Local\DXForgeEX_GuiCheckStart" }

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
	# porcelain の出力ではパスは引用されない(core.quotePath=true で日本語のパスでも確かめた)。復元の処理はいらない
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
	if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
	# worker は detach して作業するので、--delete-branch が効かずブランチが残る(#199)。接頭辞は設定で変わるので固定にしない。
	# コミットを失わないよう -D ではなく -d で消す
	$number = ($WorktreeName -replace '^gui-check-pr-', '')
	$names = @(git -C $RepoRoot for-each-ref --format='%(refname:short)' refs/heads)
	if ($LASTEXITCODE -ne 0) { Write-Host "git for-each-ref に失敗しました。ブランチが残っていないか確かめてください。"; exit 1 }
	$failed = $false
	foreach ($branch in @($names | Where-Object { $_ -match "^([^/]+/)?gui-check-pr-$number$" })) {
		git -C $RepoRoot branch -d $branch
		if ($LASTEXITCODE -ne 0) { Write-Host "ブランチ $branch を消せませんでした(未マージのコミットがあるか、ルートのチェックアウトが古い)。確かめて手で消してください。"; $failed = $true }
	}
	exit $(if ($failed) { 1 } else { 0 })
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

$mutex = $null
$held = $false
try {
	if (-not $DryRun) {
		$mutex = [System.Threading.Mutex]::new($false, $MutexName)
		try { $held = $mutex.WaitOne(0) }
		# 前の持ち主が解放せずに終わった場合。所有権はこちらに移っている
		catch [System.Threading.AbandonedMutexException] { $held = $true }
		if (-not $held) {
			Write-Host "別の start-gui-check.ps1 が起動の途中です。GUI の操作はデスクトップ全体に作用するので、同時に 1 つだけにします。終わってから確かめ直してください。"
			exit 4
		}
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
}
finally {
	if ($held) { $mutex.ReleaseMutex() }
	if ($mutex) { $mutex.Dispose() }
}
if ($code -eq 0 -and -not $DryRun) {
	Write-Output "worker_done の要約を待ってください。そのあと worker-release してから、次を実行して worktree を消します:"
	Write-Output "  ./.claude/skills/gui-check/start-gui-check.ps1 -Pr $Pr -Cleanup"
}
exit $code
