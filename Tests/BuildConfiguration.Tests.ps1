<#
.SYNOPSIS
	build.ps1 の構成の読み替えの回帰テスト(Issue #129)。MSBuild もビルドも呼ばない。

.DESCRIPTION
	build.ps1 を実行せずに PowerShell のパーサーで読み、構成を決める関数(Get-NativeOutputConfiguration・
	Get-NativeBuildConfiguration・Test-EngineUnitTestsBuiltByNative)の定義だけを取り出して、このスクリプトの中で
	呼び出す。確かめること:
	  - -Project が EngineTest/EngineUnitTests だけのときは、*Editor 構成(既定の DebugEditor を含む)を
	    Debug/Release に読み替える(#129 の MSB4057 の再現)
	  - ソリューション全体・ほかのプロジェクトが混ざるとき・Debug/Release 構成のときは、指定どおりの構成のまま
	  - 読み替えたあとは、-RunTests が EngineUnitTests をもう一度ビルドしない(#60 の追加ビルドは従来どおり)
	  - -RunTests の EngineUnitTests の追加ビルド(Invoke-TestsBuild)は、ビルドの前に Restore.ps1 を
	    その構成と -Project EngineUnitTests で呼ぶ(D3D12 Agility SDK のパッケージが要る。-Target editor では
	    ネイティブのビルドの復元が走らないため、まっさらな worktree で失敗していた、#155)。-NoRestore なら呼ばない。
	    Restore.ps1 と MSBuild は偽物に差し替える。

.PARAMETER ScriptPath
	テストするスクリプト。既定はリポジトリの build.ps1。修正前のスクリプトで再現を確かめるときは、
	git show <修正前のコミット>:build.ps1 を保存したファイルを渡す(関数が無いため失敗する)。

.EXAMPLE
	pwsh -NoProfile -File Tests/BuildConfiguration.Tests.ps1
#>
[CmdletBinding()]
param(
	[string]$ScriptPath = (Join-Path $PSScriptRoot "..\build.ps1")
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ScriptPath = (Resolve-Path $ScriptPath).Path

$failures = [System.Collections.Generic.List[string]]::new()
function Check([bool]$condition, [string]$message) {
	if ($condition) { Write-Host "  ok: $message" }
	else {
		Write-Host "  NG: $message" -ForegroundColor Red
		$failures.Add($message)
	}
}

# build.ps1 の本体はビルドを始めてしまうため、関数の定義だけを読み込む
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($ScriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) { throw "build.ps1 の構文エラー: $($parseErrors[0].Message)" }
$wanted = @("Get-NativeOutputConfiguration", "Get-NativeBuildConfiguration", "Test-EngineUnitTestsBuiltByNative", "Invoke-TestsBuild")
$functions = @($ast.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $false) |
	Where-Object { $wanted -contains $_.Name })
foreach ($name in $wanted) {
	if (-not ($functions | Where-Object Name -eq $name)) {
		Write-Host "  NG: build.ps1 に関数 $name がありません" -ForegroundColor Red
		exit 1
	}
}
foreach ($function in $functions) { . ([scriptblock]::Create($function.Extent.Text)) }

Write-Host "== Get-NativeBuildConfiguration =="
$cases = @(
	# 構成, -Project, 期待する構成
	@("DebugEditor", "EngineUnitTests", "Debug"),
	@("ReleaseEditor", "EngineUnitTests", "Release"),
	@("DebugEditor", "EngineTest", "Debug"),
	@("DebugEditor", "EngineUnitTests;EngineTest", "Debug"),
	@("DebugEditor", "EngineUnitTests:Rebuild", "Debug"),
	@("DebugEditor", "", "DebugEditor"),
	@("DebugEditor", "DXForgeEngine", "DebugEditor"),
	@("DebugEditor", "EngineUnitTests;DXForgeEngineDLL", "DebugEditor"),
	@("Debug", "EngineUnitTests", "Debug"),
	@("Release", "EngineUnitTests", "Release"),
	@("Debug", "", "Debug")
)
foreach ($case in $cases) {
	$actual = Get-NativeBuildConfiguration $case[0] $case[1]
	Check ($actual -eq $case[2]) "[$($case[0])] -Project '$($case[1])' -> $($case[2]) (実際: $actual)"
}

Write-Host "== Test-EngineUnitTestsBuiltByNative (-RunTests の追加ビルドを省くか) =="
$cases = @(
	# -Target, 構成, -Project, 期待(true = 追加ビルドしない)
	@("native", "DebugEditor", "EngineUnitTests", $true),   # #129: 読み替えたあとは1回だけビルドする
	@("native", "Debug", "EngineUnitTests", $true),
	@("native", "Release", "", $true),
	@("native", "DebugEditor", "", $false),                 # #60: *Editor のソリューション全体
	@("native", "Debug", "EngineTest", $false),             # #60: 別の -Project
	@("editor", "Debug", "", $false),                       # #60: -Target editor
	@("all", "Debug", "", $true)
)
foreach ($case in $cases) {
	$configuration = Get-NativeBuildConfiguration $case[1] $case[2]
	$actual = Test-EngineUnitTestsBuiltByNative $case[0] $configuration $case[2]
	Check ($actual -eq $case[3]) "-Target $($case[0]) [$($case[1])] -Project '$($case[2])' -> $($case[3]) (実際: $actual)"
}

Write-Host "== Invoke-TestsBuild (-RunTests の EngineUnitTests の追加ビルドの前に復元する、#155) =="
$fakeRoot = Join-Path ([System.IO.Path]::GetTempPath()) ("dxforge_build_test_" + [guid]::NewGuid().ToString("N"))
New-Item -ItemType Directory -Path $fakeRoot | Out-Null
try {
	$calls = [System.Collections.Generic.List[string]]::new()
	# Invoke-TestsBuild が使うスクリプトの変数と関数を、偽物で用意する
	$RepoRoot = $fakeRoot
	Set-Content -Path (Join-Path $fakeRoot "Restore.ps1") -Encoding utf8 -Value @'
param([string]$Configuration, [string]$Project, [string]$MSBuildPath)
$calls.Add("restore $Configuration $Project $MSBuildPath")
'@
	function Invoke-NativeBuild([string]$MSBuild, [string]$Config, [string]$TargetProject) {
		$calls.Add("build $Config $TargetProject")
		return 0
	}

	$NoRestore = $false
	$exit = Invoke-TestsBuild "fake-msbuild.exe" "Debug"
	Check ($exit -eq 0) "the exit code of the build is returned (実際: $exit)"
	Check (($calls -join " / ") -eq "restore Debug EngineUnitTests fake-msbuild.exe / build Debug EngineUnitTests") "restores (Debug, -Project EngineUnitTests) before building EngineUnitTests (実際: $($calls -join ' / '))"

	$calls.Clear()
	$NoRestore = $true
	$exit = Invoke-TestsBuild "fake-msbuild.exe" "Release"
	Check (($calls -join " / ") -eq "build Release EngineUnitTests") "-NoRestore: builds without restoring (実際: $($calls -join ' / '))"
}
finally {
	Remove-Item -Recurse -Force $fakeRoot -ErrorAction SilentlyContinue
}

if ($failures.Count -gt 0) {
	Write-Host "$($failures.Count) 件失敗" -ForegroundColor Red
	exit 1
}
Write-Host "すべて成功"
exit 0
