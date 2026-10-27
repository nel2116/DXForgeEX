<#
.SYNOPSIS
    エディタがゲームのビルドに使うエンジンのパスの決め方(GameDev.EnginePath)を確かめる回帰テスト(Issue #117)。

.DESCRIPTION
    ビルド済みの DXForgeEditor.dll を読み込み、エディタと同じコード(EnginePath.Resolve / Describe)をリフレクションで呼ぶ。
    エディタのウィンドウは開かず、エンジンのDLLも読み込まないので、GUIもGPUも不要。
    %TEMP% の下に、偽のworktreeとルートのチェックアウト(DXForgeEngine\EngineAPI があるフォルダ)を作って使う。
    ユーザー環境変数 DXFORGE_ENGINE は読みも書きもしない(値は引数として渡す)。

    確かめること:
      1. worktree の x64\DebugEditor から動く場合: DXFORGE_ENGINE(ルート)ではなく worktree を使い、食い違いを Warning にする。
      2. リポジトリの外から動く場合: これまでどおり DXFORGE_ENGINE を使う。DXFORGE_ENGINE も無効ならパスなし(ダイアログ)。
      3. DXFORGE_ENGINE と一致する場合: 大文字小文字や末尾の \ が違っても同じとみなし、Info にする。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。x64\<Configuration>\ の DXForgeEditor.dll を使う。

.EXAMPLE
    pwsh -NoProfile -File DXForgeEditor/Tests/engine_path_test.ps1
#>
[CmdletBinding()]
param(
	[ValidateSet("DebugEditor", "ReleaseEditor")]
	[string]$Configuration = "DebugEditor"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$OutDir = Join-Path $RepoRoot "x64\$Configuration"
$failures = [System.Collections.Generic.List[string]]::new()

function Check([bool]$condition, [string]$message)
{
	Write-Host ($(if ($condition) { "  PASS " } else { "  FAIL " }) + $message)
	if (-not $condition) { $failures.Add($message) }
}

$editor = [System.Reflection.Assembly]::LoadFrom((Join-Path $OutDir "DXForgeEditor.dll"))
$flags = [System.Reflection.BindingFlags]"Public,NonPublic,Static"
$enginePath = $editor.GetType("DXForgeEditor.GameDev.EnginePath", $true)
$resolve = $enginePath.GetMethod("Resolve", $flags)
$describe = $enginePath.GetMethod("Describe", $flags)

function Resolve-EnginePath([string]$editorDirectory, [string]$environmentPath)
{
	$env = if ($environmentPath) { $environmentPath } else { $null }
	$resolve.Invoke($null, @($editorDirectory, $env))
}

function Get-Description($resolution)
{
	$result = $describe.Invoke($null, @($resolution.Path, $resolution.Source, $resolution.EnvironmentPath))
	"[$($result.Item1)] $($result.Item2)"
}

function New-FakeRepository([string]$path)
{
	$null = New-Item -ItemType Directory -Force (Join-Path $path "DXForgeEngine\EngineAPI")
	$null = New-Item -ItemType Directory -Force (Join-Path $path "x64\DebugEditor")
	$path + "\"
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("dxforge_engine_path_test_" + [Guid]::NewGuid().ToString("N"))
try
{
	$root = New-FakeRepository (Join-Path $work "root")
	$worktree = New-FakeRepository (Join-Path $work "worktrees\issue-117")
	$worktreeEditor = Join-Path $worktree "x64\DebugEditor"

	Write-Host "1. Editor running from a worktree's x64\DebugEditor"
	$r = Resolve-EnginePath $worktreeEditor $root
	Check ("$($r.Source)" -eq "EditorRepository" -and $r.Path -eq $worktree) "the worktree is used instead of DXFORGE_ENGINE: $($r.Source), $($r.Path)"
	$d = Get-Description $r
	Check ($d.StartsWith("[Warning]") -and $d.Contains($worktree) -and $d.Contains($root)) "the mismatch with DXFORGE_ENGINE is a warning: $d"
	$r = Resolve-EnginePath ($worktreeEditor + "\") $null
	$d = Get-Description $r
	Check ("$($r.Source)" -eq "EditorRepository" -and $r.Path -eq $worktree -and $d.StartsWith("[Info]")) "DXFORGE_ENGINE unset (trailing \ on the editor folder): $d"
	$r = Resolve-EnginePath (Join-Path $RepoRoot "x64\$Configuration") $root
	Check ("$($r.Source)" -eq "EditorRepository" -and $r.Path -eq ($RepoRoot + "\")) "this checkout's own x64\$Configuration resolves to it: $($r.Path)"

	Write-Host "2. Editor running from outside a repository"
	$outside = Join-Path $work "tools\bin"
	$null = New-Item -ItemType Directory -Force $outside
	$r = Resolve-EnginePath $outside $root
	$d = Get-Description $r
	Check ("$($r.Source)" -eq "EnvironmentVariable" -and $r.Path -eq $root -and $d.StartsWith("[Info]")) "DXFORGE_ENGINE is used: $d"
	# x64\<構成> の形でも、2つ上に DXForgeEngine\EngineAPI がなければリポジトリとみなさない
	$noEngine = Join-Path $work "noengine\x64\DebugEditor"
	$null = New-Item -ItemType Directory -Force $noEngine
	$r = Resolve-EnginePath $noEngine $root
	Check ("$($r.Source)" -eq "EnvironmentVariable" -and $r.Path -eq $root) "x64\DebugEditor without DXForgeEngine\EngineAPI two levels up is not a repository: $($r.Source)"
	# 2つ上に EngineAPI があっても、親が x64 でなければリポジトリとみなさない
	$notX64 = Join-Path $worktree "bin\DebugEditor"
	$null = New-Item -ItemType Directory -Force $notX64
	$r = Resolve-EnginePath $notX64 $root
	Check ("$($r.Source)" -eq "EnvironmentVariable") "a folder that is not under x64 is not a repository: $($r.Source)"
	$r = Resolve-EnginePath $outside (Join-Path $work "missing\")
	Check ("$($r.Source)" -eq "None" -and $r.Path -eq "") "an invalid DXFORGE_ENGINE gives no path (EnginePathDialog is shown): $($r.Source)"
	$r = Resolve-EnginePath $outside $null
	Check ("$($r.Source)" -eq "None") "DXFORGE_ENGINE unset gives no path: $($r.Source)"
	$r = Resolve-EnginePath "" $root
	Check ("$($r.Source)" -eq "EnvironmentVariable") "an empty editor folder falls back to DXFORGE_ENGINE: $($r.Source)"

	Write-Host "3. DXFORGE_ENGINE matches the editor's repository"
	# ダイアログで選んだパスは大文字で保存される(EnginePathDialog)。末尾の \ もない場合がある
	$r = Resolve-EnginePath $worktreeEditor $worktree.ToUpper().TrimEnd("\")
	$d = Get-Description $r
	Check ("$($r.Source)" -eq "EditorRepository" -and $r.Path -eq $worktree -and $d.StartsWith("[Info]") -and $d.Contains("DXFORGE_ENGINE")) "same path (case and trailing \ ignored) is info: $d"
}
finally
{
	Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}

Write-Host ""
if ($failures.Count -gt 0)
{
	Write-Host "FAILED ($($failures.Count))"
	exit 1
}
Write-Host "ALL PASSED"
exit 0
