<#
.SYNOPSIS
    エディタからのエンジンの初期化・終了(EngineAPI.InitializeEngine / ShutdownEngine)を確かめる回帰テスト(Primal Day 078, Issue #31)。

.DESCRIPTION
    ビルド済みの DXForgeEditor.dll を読み込み、エディタと同じコードをリフレクションで呼ぶ。エディタのウィンドウは開かず、GPUも使わない。
    Logger に WPF が要るので、WPF の Application を作ってから呼ぶ(STA で実行する)。

    確かめること:
      1. EngineAPIStructs.EngineInitError の名前と値が、C++ の editor::engine_init_error(DXForgeEngineDLL/EngineInit.h)と一致する。
         基になる型は 4 バイト(C++ は u32)。
      2. 初期化に失敗したときの文言(EngineAPI.GetInitializeEngineErrorMessage)が、すべての値と範囲外の値で、例外を投げずに
         理由(Description)と作業フォルダを含む文字列を返す。
      3. DXForgeEngineDLL.dll があるとき: エディタのラッパーから DLL の関数を呼ぶ。
         - 初期化していないときの ShutdownEngine と、その2回目で落ちない。
         - 作業フォルダを %TEMP% の下の空のフォルダにし、シェーダーのソースのないエンジンのパス(または null)を渡すと
           ShaderCompilation が返る(グラフィックスは初期化しない)。そのあとの ShutdownEngine も落ちない。
         - 同じ作業フォルダで、エンジンのパスにこのリポジトリを渡すと、エンジンのシェーダーをコンパイルして作業フォルダに
           shaders.bin を書き、グラフィックスを初期化する(GPU がなければ Graphics)。ShutdownEngine を2回呼んで終える。
         初期化できたときは、EngineAPI.GetGraphicsRuntimeDescription が、DLL の隣の D3D12\ の D3D12 Agility SDK
         (DXForgeEngine/packages.config のバージョン)を使ったことを示す(#155)。DLL の隣と exe(pwsh.exe)の隣が違う場合の確認になる。
         DLL が D3D12GetInterface など古い d3d12.dll にない関数を静的にインポートしていない(render_run_test.py --check-imports。
         インポートすると Agility SDK 対応前の Windows 10 では DLL を読み込めず、OS のランタイムに戻れない、#155)。
         DLL がないとき: InitializeEngine が例外を投げずに Unknown を返し、
         ShutdownEngine も GetGraphicsRuntimeDescription も例外を投げない。-RequireEngineDll を付けると、DLL がないときは失敗にする(ローカルでは付ける)。
      初期化に成功する経路(シェーダーのコンパイル → Direct3D 12 の初期化 → リソースの作成・破棄 → 終了)は、DLL と同じ
      editor::initialize_engine を使う EngineUnitTests の --render(render_run_test.py、WARP)で確かめる。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。x64\<Configuration>\ の DXForgeEditor.dll と DXForgeEngineDLL.dll を使う。

.PARAMETER RequireEngineDll
    DXForgeEngineDLL.dll がなければ失敗にする。

.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeEditor/Tests/engine_init_test.ps1 -RequireEngineDll
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

Add-Type -AssemblyName PresentationFramework
$null = [System.Windows.Application]::new()
$engineDllPath = Join-Path $OutDir "DXForgeEngineDLL.dll"
$hasEngineDll = Test-Path $engineDllPath
if (-not $hasEngineDll)
{
	if ($RequireEngineDll) { throw "$engineDllPath がありません(./build.ps1 -Target native -Configuration $Configuration -Project DXForgeEngineDLL)。" }
	Write-Host "NOTE: $engineDllPath がないため、DLL がないときの動き(Unknown を返す)を確かめる。"
}
else
{
	$importsOutput = & python (Join-Path $RepoRoot "EngineUnitTests\Tests\render_run_test.py") --check-imports $engineDllPath 2>&1
	Check ($LASTEXITCODE -eq 0) "DXForgeEngineDLL.dll does not statically import new d3d12.dll functions: $($importsOutput -join ' / ')"
	[void][System.Runtime.InteropServices.NativeLibrary]::Load($engineDllPath)
}
if ($hasEngineDll -and $Configuration -eq "DebugEditor")
{
	# Debugのassertはモーダルなダイアログを出して止まるため、標準エラーに出してabortするようにする
	Add-Type -Namespace DXForgeTest -Name Crt -MemberDefinition @'
[DllImport("ucrtbased.dll")] public static extern int _set_error_mode(int mode);
[DllImport("ucrtbased.dll")] public static extern uint _set_abort_behavior(uint flags, uint mask);
'@
	[void][DXForgeTest.Crt]::_set_error_mode(1)
	[void][DXForgeTest.Crt]::_set_abort_behavior(0, 3)
}

$editor = [System.Reflection.Assembly]::LoadFrom((Join-Path $OutDir "DXForgeEditor.dll"))
$flags = [System.Reflection.BindingFlags]"Public,NonPublic,Static"
$engineApi = $editor.GetType("DXForgeEditor.DllWrappers.EngineAPI", $true)
$initError = $editor.GetType("DXForgeEditor.EngineAPIStructs.EngineInitError", $true)
$getMessage = $engineApi.GetMethod("GetInitializeEngineErrorMessage", $flags)
# エディタは解決済みのエンジンのパス(MainWindow.DXForgePath)を渡す(#31 の Codex の指摘)
$initialize = $engineApi.GetMethod("InitializeEngine", $flags, [Type[]]@([string]))
Check ($null -ne $initialize) "EngineAPI.InitializeEngine takes the engine path (string)"
$messageEnginePath = "C:\Engine\Path\"
$shutdown = $engineApi.GetMethod("ShutdownEngine", $flags, [Type[]]@())
$getRuntime = $engineApi.GetMethod("GetGraphicsRuntimeDescription", $flags)
Check ($null -ne $getRuntime) "EngineAPI.GetGraphicsRuntimeDescription exists"
# DXForgeEngine/packages.config の Microsoft.Direct3D.D3D12 のバージョン(例 1.620.1。D3D12_SDK_VERSION は 620)
$agilityVersion = ([xml](Get-Content -Raw (Join-Path $RepoRoot "DXForgeEngine\packages.config"))).packages.package |
	Where-Object { $_.id -eq "Microsoft.Direct3D.D3D12" } | ForEach-Object { $_.version }

function ConvertTo-PascalCase([string]$name)
{
	-join ($name -split "_" | ForEach-Object { $_.Substring(0, 1).ToUpperInvariant() + $_.Substring(1) })
}

Write-Host "1. EngineInitError matches editor::engine_init_error (EngineInit.h)"
$header = Get-Content -Raw -Encoding UTF8 (Join-Path $RepoRoot "DXForgeEngineDLL\EngineInit.h")
$enumBody = [regex]::Match($header, "enum\s+error_code\s*:\s*u32\s*\{(?<body>[^}]*)\}").Groups["body"].Value
$cppValues = [ordered]@{}
foreach ($m in [regex]::Matches($enumBody, "(?<name>\w+)\s*=\s*(?<value>\d+)"))
{
	$cppValues[(ConvertTo-PascalCase $m.Groups["name"].Value)] = [int]$m.Groups["value"].Value
}
Check ($cppValues.Count -eq 4) "EngineInit.h has 4 error codes with explicit values: $($cppValues.Keys -join ', ')"
$csNames = [Enum]::GetNames($initError)
Check ($csNames.Count -eq $cppValues.Count) "C# has the same number of values: $($csNames -join ', ')"
foreach ($name in $cppValues.Keys)
{
	$defined = $csNames -contains $name
	$csValue = if ($defined) { [int]([Enum]::Parse($initError, $name)) } else { $null }
	Check ($defined -and $csValue -eq $cppValues[$name]) "$name = $($cppValues[$name]) (C#: $csValue)"
}
$underlying = [Enum]::GetUnderlyingType($initError)
Check ($underlying -eq [int] -or $underlying -eq [uint32]) "the underlying type is 4 bytes (C++ u32): $underlying"

Write-Host "2. GetInitializeEngineErrorMessage returns a reason without throwing"
foreach ($name in $csNames)
{
	$value = [Enum]::Parse($initError, $name)
	$description = ($initError.GetField($name).GetCustomAttributes([System.ComponentModel.DescriptionAttribute], $false) | Select-Object -First 1).Description
	try
	{
		$message = $getMessage.Invoke($null, @($value, $messageEnginePath))
		if ($name -eq "Succeeded")
		{
			Check ($message -eq $description) "${name}: the description only: $message"
		}
		else
		{
			Check (-not [string]::IsNullOrEmpty($description) -and $message.StartsWith($description) -and $message.Contains("作業フォルダ: ") -and $message.Contains("エンジンのパス: $messageEnginePath")) "${name}: reason, engine path and working folder: $($message -replace "`n", ' / ')"
		}
	}
	catch
	{
		Check $false "${name}: threw $($_.Exception.InnerException.Message)"
	}
}
try
{
	$message = $getMessage.Invoke($null, @([Enum]::Parse($initError, "Graphics"), $null))
	Check (-not [string]::IsNullOrEmpty($message) -and $message.Contains("作業フォルダ: ")) "null engine path: $($message -replace "`n", ' / ')"
}
catch
{
	Check $false "null engine path: threw $($_.Exception.InnerException.Message)"
}
foreach ($raw in @(99, -1))
{
	try
	{
		$message = $getMessage.Invoke($null, @([Enum]::ToObject($initError, $raw), $messageEnginePath))
		Check (-not [string]::IsNullOrEmpty($message) -and $message.Contains("$raw") -and $message.Contains("作業フォルダ: ")) "undefined value ${raw}: $($message -replace "`n", ' / ')"
	}
	catch
	{
		Check $false "undefined value ${raw}: threw $($_.Exception.InnerException.Message)"
	}
}

Write-Host "3. InitializeEngine / ShutdownEngine through the editor's wrappers"
$previousDirectory = [System.IO.Directory]::GetCurrentDirectory()
$work = Join-Path ([System.IO.Path]::GetTempPath()) ("dxforge_engine_init_test_" + [Guid]::NewGuid().ToString("N"))
try
{
	if ($hasEngineDll)
	{
		try
		{
			$shutdown.Invoke($null, @())
			$shutdown.Invoke($null, @())
			Check $true "ShutdownEngine before InitializeEngine (twice) does nothing"
		}
		catch
		{
			Check $false "ShutdownEngine before InitializeEngine threw $($_.Exception.InnerException.Message)"
		}

		# リポジトリの外の作業フォルダ(..\..\DXForgeEngine\Graphics\Direct3D12\Shaders がない)
		$cwd = Join-Path $work "a\b"
		$null = New-Item -ItemType Directory -Force $cwd
		[System.IO.Directory]::SetCurrentDirectory($cwd)
		$emptyEngine = Join-Path $work "empty_engine"
		$null = New-Item -ItemType Directory -Force $emptyEngine
		$result = $initialize.Invoke($null, @([string]$emptyEngine))
		Check ("$result" -eq "ShaderCompilation") "InitializeEngine with an engine path without the shader sources returns ShaderCompilation: $result"
		$result = $initialize.Invoke($null, @([string]$null))
		Check ("$result" -eq "ShaderCompilation") "InitializeEngine without an engine path looks in ..\..\ of the working folder (none here): $result"
		Check (-not (Test-Path (Join-Path $cwd "shaders\d3d12\shaders.bin"))) "shaders.bin is not written"
		try
		{
			$shutdown.Invoke($null, @())
			$shutdown.Invoke($null, @())
			Check $true "ShutdownEngine after the failed InitializeEngine (twice) does nothing"
		}
		catch
		{
			Check $false "ShutdownEngine after the failed InitializeEngine threw $($_.Exception.InnerException.Message)"
		}

		# エンジンのパスにこのリポジトリを渡すと、作業フォルダがリポジトリの外でも、エンジンのシェーダーをコンパイルできる
		# (#31 の Codex の指摘)。そのあとのグラフィックスの初期化には GPU が要るので、GPU がなければ Graphics でもよい。
		$result = $initialize.Invoke($null, @([string]($RepoRoot + "\")))
		Check ("$result" -eq "Succeeded" -or "$result" -eq "Graphics") "InitializeEngine with this repository as the engine path compiles the engine shaders: $result"
		Check (Test-Path (Join-Path $cwd "shaders\d3d12\shaders.bin")) "shaders.bin is written in the working folder"
		if ("$result" -eq "Succeeded")
		{
			# D3D12 のランタイムは、pwsh.exe の隣ではなく DXForgeEngineDLL.dll の隣の D3D12\ から読む(#155)
			$runtimeArgs = [object[]]@($false)
			$description = [string]$getRuntime.Invoke($null, $runtimeArgs)
			Write-Host "    D3D12 runtime: $description"
			$sdkVersion = $agilityVersion.Split(".")[1]
			Check ($runtimeArgs[0] -eq $true) "GetGraphicsRuntimeDescription reports the Agility SDK: $description"
			Check ($description.StartsWith("Agility SDK $sdkVersion (D3D12Core $agilityVersion.")) "the Agility SDK version matches packages.config ($agilityVersion): $description"
			Check ($description.EndsWith((Join-Path $OutDir "D3D12\D3D12Core.dll") + ")")) "D3D12Core.dll is the one next to DXForgeEngineDLL.dll: $description"
		}
		else
		{
			Write-Host "    (graphics did not initialize here, so the D3D12 runtime is not checked)"
		}
		try
		{
			$shutdown.Invoke($null, @())
			$shutdown.Invoke($null, @())
			Check $true "ShutdownEngine after InitializeEngine ($result), and again"
		}
		catch
		{
			Check $false "ShutdownEngine after InitializeEngine threw $($_.Exception.InnerException.Message)"
		}

		# エンジンのパスに ACP(1252、932)で表せない文字が入っていても初期化できる(パスは UTF-16 のまま渡す。#31 の Codex の指摘)。
		# 別の作業フォルダにして、shaders.bin をコンパイルし直させる。
		$nonAcpEngine = Join-Path $work "開発_한글_ő\engine"
		$nonAcpShaders = Join-Path $nonAcpEngine "DXForgeEngine\Graphics\Direct3D12\Shaders"
		$null = New-Item -ItemType Directory -Force $nonAcpShaders
		Copy-Item -Path (Join-Path $RepoRoot "DXForgeEngine\Graphics\Direct3D12\Shaders\*") -Destination $nonAcpShaders
		$cwd2 = Join-Path $work "c\d"
		$null = New-Item -ItemType Directory -Force $cwd2
		[System.IO.Directory]::SetCurrentDirectory($cwd2)
		$result = $initialize.Invoke($null, @([string]$nonAcpEngine))
		Check ("$result" -eq "Succeeded" -or "$result" -eq "Graphics") "InitializeEngine with an engine path outside the ANSI code page compiles the engine shaders: $result"
		Check (Test-Path (Join-Path $cwd2 "shaders\d3d12\shaders.bin")) "shaders.bin is written in the working folder"
		try
		{
			$shutdown.Invoke($null, @())
			Check $true "ShutdownEngine after InitializeEngine ($result)"
		}
		catch
		{
			Check $false "ShutdownEngine after InitializeEngine threw $($_.Exception.InnerException.Message)"
		}
	}
	else
	{
		try
		{
			$result = $initialize.Invoke($null, @([string]($RepoRoot + "\")))
			Check ("$result" -eq "Unknown") "without DXForgeEngineDLL.dll, InitializeEngine returns Unknown: $result"
			$shutdown.Invoke($null, @())
			Check $true "without DXForgeEngineDLL.dll, ShutdownEngine does not throw"
			$runtimeArgs = [object[]]@($true)
			$description = [string]$getRuntime.Invoke($null, $runtimeArgs)
			Check ($description -eq "" -and $runtimeArgs[0] -eq $false) "without DXForgeEngineDLL.dll, GetGraphicsRuntimeDescription returns an empty string"
		}
		catch
		{
			Check $false "without DXForgeEngineDLL.dll, the wrappers threw $($_.Exception.InnerException.Message)"
		}
	}
}
finally
{
	[System.IO.Directory]::SetCurrentDirectory($previousDirectory)
	Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}

Write-Host "----"
if ($failures.Count -gt 0)
{
	Write-Host "FAILED ($($failures.Count)):"
	$failures | ForEach-Object { Write-Host "  $_" }
	exit 1
}
Write-Host "All engine initialization checks passed ($Configuration)."
exit 0
