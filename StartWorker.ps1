<#
.SYNOPSIS
	共通のルール(docs/dev/WorkerRules.md)と仕様を必ず連結して、Orca のワーカーを起動するスクリプト(#195)。

.DESCRIPTION
	orca orchestration worker-start を包む。コーディネーターが共通のルールを貼り忘れることと、小さな仕事に
	大きなモデルを使うことを防ぐ。orca の出力と終了コードをそのまま返す。

.PARAMETER SpecFile
	タスクの仕様のファイル(UTF-8)。

.PARAMETER Size
	normal(既定)または small。small は --model claude-sonnet-5-5 を付ける。

.PARAMETER Name
	新しい worktree の名前(例: issue-195-review-flow)。

.PARAMETER Worktree
	--worktree に渡す値。既定は new-top-level。既存の worktree(path:...、current など)を選ぶと、
	worktree を作るときの引数(--name、--setup)は付けない。

.PARAMETER Run
	Run の ID。省略すると、このコーディネーターに結び付いている Run。

.PARAMETER RetryOf
	起動し直すときの、前の Dispatch の ID(--retry-of)。

.PARAMETER DryRun
	実行せず、orca の引数と連結した仕様を表示する。

.EXAMPLE
	./StartWorker.ps1 -SpecFile $env:TEMP\spec.md -Name issue-200-foo -Size small
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[string]$SpecFile,

	[ValidateSet("normal", "small")]
	[string]$Size = "normal",

	[string]$Name,

	[string]$Worktree = "new-top-level",

	[string]$Run,

	[string]$RetryOf,

	[switch]$DryRun
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

$SmallModel = "claude-sonnet-5-5"
$rulesPath = Join-Path $PSScriptRoot "docs/dev/WorkerRules.md"

foreach ($path in $rulesPath, $SpecFile) {
	if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
		Write-Host "ファイルがありません: $path"
		exit 2
	}
}
$rules = [System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $rulesPath).Path, [System.Text.Encoding]::UTF8).Trim()
$spec = [System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $SpecFile).Path, [System.Text.Encoding]::UTF8).Trim()
if (-not $rules -or -not $spec) {
	Write-Host "共通のルールまたは仕様が空です。"
	exit 2
}

$isNewWorktree = $Worktree -like "new-*"
if ($isNewWorktree -and -not $Name) {
	Write-Host "新しい worktree には -Name が要ります(例: issue-200-foo)。"
	exit 2
}

$arguments = @("orchestration", "worker-start", "--agent", "claude", "--worktree", $Worktree)
if ($isNewWorktree) { $arguments += @("--name", $Name, "--setup", "run") }
if ($Size -eq "small") { $arguments += @("--model", $SmallModel) }
if ($Run) { $arguments += @("--run", $Run) }
if ($RetryOf) { $arguments += @("--retry-of", $RetryOf) }

$combined = "$rules`n`n$spec`n"
if ($DryRun) {
	Write-Output "orca $($arguments -join ' ') --spec-file <連結した仕様>"
	Write-Output $combined
	exit 0
}

if (-not (Get-Command orca -ErrorAction SilentlyContinue)) {
	Write-Host "orca が見つかりません。"
	exit 1
}
$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("dxforge_worker_spec_" + [guid]::NewGuid().ToString("N") + ".md")
try {
	[System.IO.File]::WriteAllText($temp, $combined, [System.Text.UTF8Encoding]::new($false))
	& orca @arguments --spec-file $temp
	exit $LASTEXITCODE
}
finally {
	Remove-Item -LiteralPath $temp -ErrorAction SilentlyContinue
}
