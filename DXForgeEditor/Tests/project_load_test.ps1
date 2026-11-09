<#
.SYNOPSIS
    プロジェクトを開けないとき、その理由が Logger と Project.LastLoadError に残ることを確かめる回帰テスト(Issue #250)。

.DESCRIPTION
    ビルド済みの DXForgeEditor.dll を読み込み、エディタと同じ Project.Load(ヘッドレスではない経路)をリフレクションで呼ぶ。
    エディタのウィンドウは開かず、GPU もエンジンも使わない。Logger に WPF が要るので、WPF の Application を作ってから呼ぶ。
    ファイルは %TEMP% の下に作り、最後に消す。

    確かめること(壊れた .dxproj をそれぞれ Project.Load に渡す):
      1. XML として壊れているファイル / ルートが i:nil="true" のファイル: null が返り(例外は出ない)、
         Logger のエラーに例外の型とメッセージが入り、Project.LastLoadError にも同じ理由が入る。
      2. 入れ子の例外(InnerException)の理由もログに入る。
      3. 読み込みに失敗したあとの Project.Current は null。
      4. 失敗の後始末(Project.ReleaseEntities): 読み込んだプロジェクトのエンティティをエンジンに作ったあと呼ぶと、すべてエンジンから
         外れる。Project.Load の catch がこれを呼ぶので、読み込みに失敗してもエンジンにエンティティが残らない(残ると終了時に
         FreeList.h の assert(!_size) で止まる)。Load 全体で失敗させるとゲームコードのビルド(Visual Studio)が走るので、ヘッドレスの
         読み込みで作ったプロジェクトに直接呼ぶ。DXForgeEngineDLL.dll が要る(エンジンの初期化は要らない)。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。

.PARAMETER RequireEngineDll
    DXForgeEngineDLL.dll がなければ失敗にする(失敗後の後始末の項目を SKIP にしない)。

.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeEditor/Tests/project_load_test.ps1
#>
[CmdletBinding()]
param(
	[ValidateSet("DebugEditor", "ReleaseEditor")]
	[string]$Configuration = "DebugEditor",
	[switch]$RequireEngineDll
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

if ([System.Threading.Thread]::CurrentThread.GetApartmentState() -ne "STA") { throw "Run this script on an STA thread (pwsh -STA)." }

$engineDllPath = Join-Path $OutDir "DXForgeEngineDLL.dll"
$hasEngineDll = Test-Path $engineDllPath
if (-not $hasEngineDll -and $RequireEngineDll) { throw "$engineDllPath がありません(./build.ps1 -Target native -Configuration $Configuration -Project DXForgeEngineDLL)。" }
if ($hasEngineDll)
{
	[void][System.Runtime.InteropServices.NativeLibrary]::Load($engineDllPath)
	if ($Configuration -eq "DebugEditor")
	{
		# Debugのassertはモーダルなダイアログを出して止まるため、標準エラーに出してabortするようにする
		Add-Type -Namespace DXForgeTest -Name Crt -MemberDefinition @'
[DllImport("ucrtbased.dll")] public static extern int _set_error_mode(int mode);
[DllImport("ucrtbased.dll")] public static extern uint _set_abort_behavior(uint flags, uint mask);
'@
		[void][DXForgeTest.Crt]::_set_error_mode(1)
		[void][DXForgeTest.Crt]::_set_abort_behavior(0, 3)
	}
}

Add-Type -AssemblyName PresentationFramework
$app = [System.Windows.Application]::new()
$editor = [System.Reflection.Assembly]::LoadFrom((Join-Path $OutDir "DXForgeEditor.dll"))
$flags = [System.Reflection.BindingFlags]"Public,NonPublic,Static,Instance"
$projectType = $editor.GetType("DXForgeEditor.GameProject.Project", $true)
$loggerType = $editor.GetType("DXForgeEditor.Utilities.Logger", $true)
$load = $projectType.GetMethod("Load", $flags)
$current = $projectType.GetProperty("Current", $flags)
$lastError = $projectType.GetProperty("LastLoadError", $flags)
$messages = $loggerType.GetProperty("Messages", $flags).GetValue($null)

# Logger.Log は Dispatcher に積むので、積まれた分を処理してから読む
function Flush-Logger { $app.Dispatcher.Invoke([action]{}, [System.Windows.Threading.DispatcherPriority]::Background) }
function Get-ErrorLogs
{
	Flush-Logger
	return @($messages | Where-Object { $_.MessageType.ToString() -eq "Error" } | ForEach-Object { $_.Message })
}

$temp = Join-Path ([System.IO.Path]::GetTempPath()) ("project_load_test_" + [guid]::NewGuid().ToString("N"))
[void](New-Item -ItemType Directory -Path $temp)
try
{
	# Project の [DataContract(Name = "Game")] のルート要素
	$ns = "http://schemas.datacontract.org/2004/07/DXForgeEditor.GameProject"
	$cases = @(
		@{ Name = "invalid XML"; Content = "<Game xmlns=`"$ns`"><Name>broken"; Expect = "XmlException" },
		@{ Name = "wrong root"; Content = "<Project><Name>x</Name></Project>"; Expect = "SerializationException" },
		@{ Name = "nil root"; Content = "<Game xmlns=`"$ns`" xmlns:i=`"http://www.w3.org/2001/XMLSchema-instance`" i:nil=`"true`" />"; Expect = "InvalidDataException" }
	)
	foreach ($case in $cases)
	{
		Write-Host "[$($case.Name)]"
		$dir = Join-Path $temp ($case.Name -replace '\W', '_')
		[void](New-Item -ItemType Directory -Path $dir)
		[void](New-Item -ItemType Directory -Path (Join-Path $dir "Content"))
		$file = Join-Path $dir "Broken.dxproj"
		[System.IO.File]::WriteAllText($file, $case.Content)
		$before = (Get-ErrorLogs).Count

		$result = $null
		$threw = $false
		try { $result = $load.Invoke($null, [object[]]@([string]$file)) } catch { $threw = $true; Write-Host $_ }
		Check (-not $threw) "Project.Load は例外を投げない"
		Check ($null -eq $result) "null が返る"
		Check ($null -eq $current.GetValue($null)) "Project.Current は null のまま"

		$logs = @(Get-ErrorLogs | Select-Object -Skip $before)
		Write-Host "  log: $($logs -join ' | ')"
		Check ($logs.Count -ge 1) "エラーがログに出る"
		Check (($logs -join "`n").Contains($case.Expect)) "ログに例外の型($($case.Expect))が入る"
		Check ($null -ne $lastError -and -not [string]::IsNullOrEmpty($lastError.GetValue($null)) -and ([string]$lastError.GetValue($null)).Contains($case.Expect)) "LastLoadError に例外の型が入る"
	}

	# ほかのプロセスが開いたままのファイル(IOException)。Serializer.FromFile は FileShare.Read で開くので、書き込みで開かれていると失敗する
	Write-Host "[locked file]"
	$lockDir = Join-Path $temp "locked"
	[void](New-Item -ItemType Directory -Path $lockDir)
	[void](New-Item -ItemType Directory -Path (Join-Path $lockDir "Content"))
	$lockFile = Join-Path $lockDir "Locked.dxproj"
	$lockStream = [System.IO.FileStream]::new($lockFile, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write, [System.IO.FileShare]::None)
	try
	{
		$result = $load.Invoke($null, [object[]]@([string]$lockFile))
		Check ($null -eq $result) "null が返る"
		Check (([string]$lastError.GetValue($null)).Contains("IOException")) "LastLoadError に IOException が入る"
		Check ((Get-ErrorLogs | Select-Object -Last 1).Contains("IOException")) "ログに IOException が入る"
	}
	finally { $lockStream.Dispose() }

	Write-Host "[inner exception]"
	$describe = $projectType.GetMethod("DescribeException", $flags)
	Check ($null -ne $describe) "DescribeException がある"
	if ($null -ne $describe)
	{
		$ex = [System.InvalidOperationException]::new("outer message", [System.IO.IOException]::new("inner message"))
		$text = [string]$describe.Invoke($null, [object[]]@($ex))
		Check ($text.Contains("InvalidOperationException") -and $text.Contains("outer message")) "外側の例外の型とメッセージが入る"
		Check ($text.Contains("IOException") -and $text.Contains("inner message")) "内側の例外の型とメッセージが入る"
	}

	Write-Host "[release entities after a failed load]"
	if (-not $hasEngineDll) { Write-Host "  SKIP (DXForgeEngineDLL.dll がない)" }
	else
	{
		# ヘッドレスの読み込みはエンティティをエンジンに作らないので、作ってから ReleaseEntities を呼ぶ(以降、非ヘッドレスの Load は使わない)
		$editor.GetType("DXForgeEditor.GameProject.ProjectLoadEnvironment", $true).GetProperty("Headless", $flags).SetValue($null, $true)
		$workDir = Join-Path $temp "TransformOnly"
		[void](New-Item -ItemType Directory -Path $workDir)
		$workFile = Join-Path $workDir "TransformOnly.dxproj"
		Copy-Item (Join-Path $RepoRoot "DXForgeCli\Tests\Projects\TransformOnly\TransformOnly.dxproj") $workFile
		$project = $load.Invoke($null, [object[]]@([string]$workFile)).PSObject.BaseObject
		$entities = @($project.ActiveScene.GameEntities)
		foreach ($e in $entities) { $e.IsActive = $true }
		Check (($entities.Count -ge 3) -and (@($entities | Where-Object { $_.EntityId -ge 0 }).Count -eq $entities.Count)) "エンティティがエンジンに作られた($($entities.Count) 件)"
		$release = $projectType.GetMethod("ReleaseEntities", $flags)
		Check ($null -ne $release) "ReleaseEntities がある"
		if ($null -ne $release)
		{
			$threw = $false
			try { $release.Invoke($project, @()) } catch { $threw = $true; Write-Host $_ }
			Check (-not $threw) "ReleaseEntities は例外を投げない"
			Check (@($entities | Where-Object { $_.IsActive -or $_.EntityId -ge 0 }).Count -eq 0) "すべてのエンティティがエンジンから外れた"
			$release.Invoke($project, @())
			Check $true "もう一度呼んでも何も起きない"
		}
	}
}
finally
{
	Remove-Item -Recurse -Force $temp -ErrorAction SilentlyContinue
}

if ($failures.Count -gt 0)
{
	Write-Host "`nFAILED: $($failures.Count) 件"
	exit 1
}
Write-Host "`nAll project load checks passed."
exit 0
