<#
.SYNOPSIS
	Restore.ps1 の「NuGetの復元が要るか」の判定の回帰テスト(Issue #116)。本物の MSBuild とネットワークには触らない。

.DESCRIPTION
	ケースごとに %TEMP% の下へ使い捨てのフォルダを作り、テスト対象のスクリプトと、ダミーの DXForge.sln と
	プロジェクト(SDK 形式の DXForgeEditor・DXForgeImport、SDK 形式でない Legacy.csproj、Native.vcxproj)を置いて、
	-MSBuildPath に偽の MSBuild(呼ばれた引数をファイルに書くだけ)を渡して実行する。
	packages.config は置かないため、復元が要るかどうかは obj/project.assets.json の有無だけで決まる。

	ケース:
	  fresh         両方の obj がないとき、復元し、両方の名前を出す
	  import-only   DXForgeEditor だけ復元済みのとき、DXForgeImport を復元する(#116 の再現)
	  all-restored  両方とも復元済みのとき、MSBuild を呼ばず「復元は不要です」と出す
	  non-editor    Debug 構成では、obj がなくても MSBuild を呼ばない

.PARAMETER ScriptPath
	テストするスクリプト。既定はリポジトリの Restore.ps1。修正前のスクリプトで再現を確かめるときは、
	git show <修正前のコミット>:Restore.ps1 を保存したファイルを渡す(import-only が失敗する)。

.PARAMETER Case
	実行するケースの名前(省略するとすべて)。

.EXAMPLE
	pwsh -NoProfile -File Tests/Restore.Tests.ps1
#>
[CmdletBinding()]
param(
	[string]$ScriptPath = (Join-Path $PSScriptRoot "..\Restore.ps1"),
	[string[]]$Case
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ScriptPath = (Resolve-Path $ScriptPath).Path

$failures = [System.Collections.Generic.List[string]]::new()
$currentCase = ""
function Check([bool]$condition, [string]$message) {
	if ($condition) { Write-Host "  ok: $message" }
	else {
		Write-Host "  NG: $message" -ForegroundColor Red
		$failures.Add("[$currentCase] $message")
	}
}

# 偽の MSBuild(テスト用)。呼ばれた引数を DXFORGE_TEST_MSBUILD_LOG のファイルに書いて成功する
$fakeMSBuild = @'
Add-Content -LiteralPath $env:DXFORGE_TEST_MSBUILD_LOG -Value ($args -join ' ')
exit 0
'@

$solution = @'
Microsoft Visual Studio Solution File, Format Version 12.00
Project("{9A19103F-16F7-4668-BE54-9A1E7A4F7556}") = "DXForgeEditor", "DXForgeEditor\DXForgeEditor.csproj", "{9BE3E628-EF34-44E3-8C91-A35459C6CF69}"
EndProject
Project("{8BC9CEB8-8B4A-11D0-8D11-00A0C91BC942}") = "Native", "Native\Native.vcxproj", "{A7DBD67B-496C-463E-97D0-D522150BB52F}"
EndProject
Project("{FAE04EC0-301F-11D3-BF4B-00C04F79EFBC}") = "Legacy", "Legacy\Legacy.csproj", "{184FC280-4B34-4698-990D-AC18609E845D}"
EndProject
Project("{9A19103F-16F7-4668-BE54-9A1E7A4F7556}") = "DXForgeImport", "DXForgeImport\DXForgeImport.csproj", "{5C6B1E2A-7D3F-4B8E-9A41-2F0D6C8E1B37}"
EndProject
Global
EndGlobal
'@

function New-Sandbox {
	$dir = Join-Path ([IO.Path]::GetTempPath()) "restore-test-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
	New-Item -ItemType Directory $dir | Out-Null
	Copy-Item -LiteralPath $ScriptPath (Join-Path $dir "Restore.ps1")
	Set-Content -LiteralPath (Join-Path $dir "DXForge.sln") -Value $solution
	$projects = [ordered]@{
		"DXForgeEditor\DXForgeEditor.csproj" = '<Project Sdk="Microsoft.NET.Sdk"></Project>'
		"DXForgeImport\DXForgeImport.csproj" = '<Project Sdk="Microsoft.NET.Sdk"></Project>'
		# SDK 形式でない .csproj は project.assets.json を作らないため、obj がなくても復元の理由にならない
		"Legacy\Legacy.csproj" = '<Project ToolsVersion="15.0" xmlns="http://schemas.microsoft.com/developer/msbuild/2003"></Project>'
		"Native\Native.vcxproj" = '<Project DefaultTargets="Build" xmlns="http://schemas.microsoft.com/developer/msbuild/2003"></Project>'
	}
	foreach ($path in $projects.Keys) {
		$file = Join-Path $dir $path
		New-Item -ItemType Directory (Split-Path $file -Parent) -Force | Out-Null
		Set-Content -LiteralPath $file -Value $projects[$path]
	}
	$tools = Join-Path $dir "_tools"
	New-Item -ItemType Directory $tools | Out-Null
	Set-Content -LiteralPath (Join-Path $tools "msbuild.ps1") -Value $fakeMSBuild
	[ordered]@{ Dir = $dir; MSBuild = (Join-Path $tools "msbuild.ps1"); Log = (Join-Path $tools "msbuild.log") }
}

function Set-Restored($sb, [string]$project) {
	$obj = Join-Path $sb.Dir "$project\obj"
	New-Item -ItemType Directory $obj -Force | Out-Null
	Set-Content -LiteralPath (Join-Path $obj "project.assets.json") -Value "{}"
}

# Restore.ps1 を同じプロセスで実行し、Write-Host の出力と MSBuild の呼び出しを返す
# (子のプロセスにしないのは、日本語の出力がコンソールのエンコーディングに左右されないようにするため)
function Invoke-Restore($sb, [string]$Configuration, [string]$Project) {
	$env:DXFORGE_TEST_MSBUILD_LOG = $sb.Log
	$arguments = @{ Configuration = $Configuration; MSBuildPath = $sb.MSBuild }
	if ($Project) { $arguments.Project = $Project }
	$output = & (Join-Path $sb.Dir "Restore.ps1") @arguments 6>&1 | Out-String
	Write-Host "  --- 出力"
	$output.TrimEnd() -split "`r?`n" | ForEach-Object { Write-Host "  | $_" }
	$calls = @(if (Test-Path $sb.Log) { Get-Content $sb.Log })
	[ordered]@{ Text = $output; Calls = $calls }
}

function Remove-Sandbox($sb) {
	Remove-Item -LiteralPath $sb.Dir -Recurse -Force -ErrorAction SilentlyContinue
}

$cases = [ordered]@{}

$cases["fresh"] = {
	$sb = New-Sandbox
	try {
		$r = Invoke-Restore $sb -Configuration DebugEditor
		Check ($r.Calls.Count -eq 1) "MSBuild を1回呼ぶ(呼んだ回数 $($r.Calls.Count))"
		Check ($r.Calls[0] -match '/t:restore') "MSBuild に /t:restore を渡す"
		Check ($r.Text -match 'DXForgeEditor\(project\.assets\.json\)') "DXForgeEditor の名前を出す"
		Check ($r.Text -match 'DXForgeImport\(project\.assets\.json\)') "DXForgeImport の名前を出す"
		Check ($r.Text -notmatch 'Legacy|Native') "SDK 形式でないプロジェクトは対象にしない"
	}
	finally { Remove-Sandbox $sb }
}

$cases["import-only"] = {
	$sb = New-Sandbox
	try {
		Set-Restored $sb "DXForgeEditor"
		$r = Invoke-Restore $sb -Configuration DebugEditor
		Check ($r.Calls.Count -eq 1) "MSBuild を1回呼ぶ(呼んだ回数 $($r.Calls.Count))"
		Check ($r.Text -match 'DXForgeImport\(project\.assets\.json\)') "DXForgeImport の名前を出す"
		Check ($r.Text -notmatch 'DXForgeEditor\(project\.assets\.json\)') "復元済みの DXForgeEditor の名前は出さない"
	}
	finally { Remove-Sandbox $sb }
}

$cases["all-restored"] = {
	$sb = New-Sandbox
	try {
		Set-Restored $sb "DXForgeEditor"
		Set-Restored $sb "DXForgeImport"
		$r = Invoke-Restore $sb -Configuration ReleaseEditor
		Check ($r.Calls.Count -eq 0) "MSBuild を呼ばない(呼んだ回数 $($r.Calls.Count))"
		Check ($r.Text -match '復元は不要です') "「復元は不要です」と出す"
	}
	finally { Remove-Sandbox $sb }
}

$cases["non-editor"] = {
	$sb = New-Sandbox
	try {
		# -Project に EngineTest を含めないので、DXC も取得しない(GetDXC.ps1 はサンドボックスにない)
		$r = Invoke-Restore $sb -Configuration Debug -Project EngineUnitTests
		Check ($r.Calls.Count -eq 0) "MSBuild を呼ばない(呼んだ回数 $($r.Calls.Count))"
		Check ($r.Text -match '復元は不要です') "「復元は不要です」と出す"
	}
	finally { Remove-Sandbox $sb }
}

foreach ($name in $Case) {
	if ($name -notin $cases.Keys) { $failures.Add("ケースがありません: $name") }
}
foreach ($name in $cases.Keys) {
	if ($Case -and $name -notin $Case) { continue }
	$currentCase = $name
	Write-Host "== $name"
	try { & $cases[$name] }
	catch {
		Write-Host "  NG: $_" -ForegroundColor Red
		$failures.Add("[$name] $_")
	}
}

Write-Host ""
if ($failures.Count) {
	Write-Host "失敗: $($failures.Count) 件" -ForegroundColor Red
	$failures | ForEach-Object { Write-Host "  $_" }
	exit 1
}
Write-Host "すべて成功しました。"
exit 0
