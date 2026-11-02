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
         - 作業フォルダを %TEMP% の下の空のフォルダにすると、エンジンのシェーダーのソースが見つからず ShaderCompilation が返る
           (グラフィックスは初期化しないので GPU は要らない)。そのあとの ShutdownEngine も落ちない。
         DLL がないとき(CI のランナーには ATL がなく DLL をビルドできない): InitializeEngine が例外を投げずに Unknown を返し、
         ShutdownEngine も例外を投げない。-RequireEngineDll を付けると、DLL がないときは失敗にする(ローカルでは付ける)。
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
$initialize = $engineApi.GetMethod("InitializeEngine", $flags, [Type[]]@())
$shutdown = $engineApi.GetMethod("ShutdownEngine", $flags, [Type[]]@())

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
		$message = $getMessage.Invoke($null, @($value))
		if ($name -eq "Succeeded")
		{
			Check ($message -eq $description) "${name}: the description only: $message"
		}
		else
		{
			Check (-not [string]::IsNullOrEmpty($description) -and $message.StartsWith($description) -and $message.Contains("作業フォルダ: ")) "${name}: reason and working folder: $($message -replace "`n", ' / ')"
		}
	}
	catch
	{
		Check $false "${name}: threw $($_.Exception.InnerException.Message)"
	}
}
foreach ($raw in @(99, -1))
{
	try
	{
		$message = $getMessage.Invoke($null, @([Enum]::ToObject($initError, $raw)))
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

		# ..\..\DXForgeEngine\Graphics\Direct3D12\Shaders がない作業フォルダ
		$cwd = Join-Path $work "a\b"
		$null = New-Item -ItemType Directory -Force $cwd
		[System.IO.Directory]::SetCurrentDirectory($cwd)
		$result = $initialize.Invoke($null, @())
		Check ("$result" -eq "ShaderCompilation") "InitializeEngine without the shader sources returns ShaderCompilation: $result"
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
	}
	else
	{
		try
		{
			$result = $initialize.Invoke($null, @())
			Check ("$result" -eq "Unknown") "without DXForgeEngineDLL.dll, InitializeEngine returns Unknown: $result"
			$shutdown.Invoke($null, @())
			Check $true "without DXForgeEngineDLL.dll, ShutdownEngine does not throw"
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
