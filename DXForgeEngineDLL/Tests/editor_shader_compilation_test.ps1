<#
.SYNOPSIS
    エディタのDllWrappers(EngineAPI.CompileShader など)を通して、シェーダのコンパイルを確かめる回帰テスト(Primal Day 076, Issue #29)。

.DESCRIPTION
    ビルド済みの DXForgeEditor.dll を読み込み、エディタと同じコード(DefaultAssets.CreateShaderGroup、
    EngineAPI.CompileShader / AddShaderGroup / RemoveShaderGroup、ShaderGroup)をリフレクションで呼ぶ。
    エディタのウィンドウは開かないので、GUIは不要(GPUも不要)。
    Logger は WPF の Dispatcher にメッセージを積むため、WPF の Application を作ってから呼び、最後に Dispatcher を回して
    ログ(LoggerView に出るメッセージ)を読む。

    確かめること:
      1. 既定のマテリアル用シェーダ(MainVS x 3変種、MainPS)が、エディタと同じ引数でコンパイルでき、エンジンに登録できる。
         同じシェーダを2回登録しても同じID(参照カウント)。ToBinary/FromBinary で元に戻る。
      2. わざと壊したシェーダでは、CompileShader が例外を投げずに false を返し、Errors にDXCのエラーが入り、
         エラーがログに出る。エンジンへの登録(UploadToEngine)も例外を投げずに無効なIDを返す。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。x64\<Configuration>\ の DXForgeEditor.dll と DXForgeEngineDLL.dll を使う。

.EXAMPLE
    pwsh -NoProfile -File DXForgeEngineDLL/Tests/editor_shader_compilation_test.ps1
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

if ([System.Threading.Thread]::CurrentThread.GetApartmentState() -ne "STA") { throw "Run this script on an STA thread (pwsh -STA)." }

Add-Type -AssemblyName PresentationFramework
$null = [System.Windows.Application]::new()
[void][System.Runtime.InteropServices.NativeLibrary]::Load((Join-Path $OutDir "DXForgeEngineDLL.dll"))
$editor = [System.Reflection.Assembly]::LoadFrom((Join-Path $OutDir "DXForgeEditor.dll"))

$flags = [System.Reflection.BindingFlags]"Public,NonPublic,Static,Instance"
$engineApi = $editor.GetType("DXForgeEditor.DllWrappers.EngineAPI", $true)
$defaultAssets = $editor.GetType("DXForgeEditor.Content.DefaultAssets", $true)
$shaderGroupType = $editor.GetType("DXForgeEditor.Content.ShaderGroup", $true)
$shaderType = $editor.GetType("DXForgeEditor.Content.ShaderType", $true)
$logger = $editor.GetType("DXForgeEditor.Utilities.Logger", $true)

$compileShader = $engineApi.GetMethod("CompileShader", $flags, $null, [type[]]@($shaderGroupType), $null)
$createShaderGroup = $defaultAssets.GetMethod("CreateShaderGroup", $flags)
$getEngineShadersPath = $defaultAssets.GetMethod("GetEngineShadersPath", $flags)
$resourceName = $defaultAssets.GetField("DefaultMaterialShadersResource", $flags).GetValue($null)

function New-ShaderGroup([string]$type, [string]$code, [string]$function, [string[]]$defines, [uint32[]]$keys)
{
	$enginePath = $RepoRoot + "\"
	$createShaderGroup.Invoke($null, @([Enum]::Parse($shaderType, $type), $code, $function, $defines, $keys, $getEngineShadersPath.Invoke($null, @($enginePath))))
}

function Get-LogMessages
{
	# Logger.Log は Dispatcher.BeginInvoke で積むので、積まれた処理を実行してからメッセージを読む
	$frame = [System.Windows.Threading.DispatcherFrame]::new()
	$exit = [System.Windows.Threading.DispatcherOperationCallback] { param($f) $f.Continue = $false; $null }
	[void][System.Windows.Threading.Dispatcher]::CurrentDispatcher.BeginInvoke([System.Windows.Threading.DispatcherPriority]::Background, $exit, $frame)
	[System.Windows.Threading.Dispatcher]::PushFrame($frame)
	@($logger.GetField("_messages", $flags).GetValue($null) | ForEach-Object { "[$($_.MessageType)] $($_.Message)" })
}

$code = Get-Content (Join-Path $RepoRoot ("DXForgeEditor\" + $resourceName.Replace("/", "\"))) -Raw -Encoding utf8
$invalidId = -1

Write-Host "1. Default material shaders (DXForgeEditor $Configuration)"
$vs = New-ShaderGroup "Vertex" $code "MainVS" @("ELEMENTS_TYPE=0", "ELEMENTS_TYPE=1", "ELEMENTS_TYPE=3") @(0, 1, 3)
$ps = New-ShaderGroup "Pixel" $code "MainPS" @("") @([uint32]::MaxValue)
$watch = [System.Diagnostics.Stopwatch]::StartNew()
$vsOk = $compileShader.Invoke($null, @($vs))
$psOk = $compileShader.Invoke($null, @($ps))
Write-Host "  (compiled 4 shaders in $($watch.ElapsedMilliseconds) ms)"
Check ($vsOk -and $vs.IsCompiled -and $vs.Count -eq 3) "MainVS: CompileShader = $vsOk, bytes = $(($vs.ByteCode | ForEach-Object Length) -join ', ')"
Check ($psOk -and $ps.IsCompiled -and $ps.Count -eq 1) "MainPS: CompileShader = $psOk, bytes = $($ps.ByteCode[0].Length)"
Check (($vs.Errors + $ps.Errors | Where-Object { $_ }).Count -eq 0) "no errors or warnings"
Check (($vs.Assembly + $ps.Assembly | Where-Object { -not $_ }).Count -eq 0) "disassembly is returned for every variant"
Check (($vs.ExtraArgs[1] -join " ") -eq "-D ELEMENTS_TYPE=1 -I $RepoRoot\DXForgeEngine\Graphics\Direct3D12\Shaders") "the engine shaders folder is passed with -I: $($vs.ExtraArgs[1] -join ' ')"

$vsId = $vs.UploadToEngine()
$psId = $ps.UploadToEngine()
$vs2 = New-ShaderGroup "Vertex" $code "MainVS" @("ELEMENTS_TYPE=0", "ELEMENTS_TYPE=1", "ELEMENTS_TYPE=3") @(0, 1, 3)
[void]$compileShader.Invoke($null, @($vs2))
$vs2Id = $vs2.UploadToEngine()
Check ($vsId -ne $invalidId -and $psId -ne $invalidId -and $vsId -ne $psId) "UploadToEngine: vertex = $vsId, pixel = $psId"
Check ($vs2Id -eq $vsId) "the same shaders are uploaded once (reference counted): $vs2Id"

$stream = [System.IO.MemoryStream]::new()
$writer = [System.IO.BinaryWriter]::new($stream)
$vs.ToBinary($writer)
$writer.Flush()
$stream.Position = 0
$read = [Activator]::CreateInstance($shaderGroupType)
$read.FromBinary([System.IO.BinaryReader]::new($stream))
Check ($read.Count -eq 3 -and $read.FunctionName -eq "MainVS" -and $read.Code -eq $vs.Code -and
	[Convert]::ToBase64String($read.PackForEngine()) -eq [Convert]::ToBase64String($vs.PackForEngine()) -and
	($read.ExtraArgs[2] -join " ") -eq ($vs.ExtraArgs[2] -join " ") -and $read.Assembly[1] -eq $vs.Assembly[1]) "ToBinary / FromBinary round trip"

$vs2.UnloadFromEngine()
$vs.UnloadFromEngine()
$ps.UnloadFromEngine()
Check ($vs.ContentId -eq $invalidId -and $ps.ContentId -eq $invalidId) "UnloadFromEngine"
$before = (Get-LogMessages).Count

Write-Host "2. Broken shaders"
$broken = $code.Replace("PixelOut psOut;", "PixelOut psOut = ;")
$bad = New-ShaderGroup "Pixel" $broken "MainPS" @("") @([uint32]::MaxValue)
$badOk = $compileShader.Invoke($null, @($bad))
Check (-not $badOk -and -not $bad.IsCompiled -and $bad.Count -eq 1 -and $bad.ByteCode[0].Length -eq 0 -and $bad.Errors[0] -match "error: expected expression") "syntax error: CompileShader = $badOk, $(($bad.Errors[0] -split "`n")[0])"
$missing = New-ShaderGroup "Vertex" $code "NoSuchFunction" @("ELEMENTS_TYPE=0", "ELEMENTS_TYPE=1") @(0, 1)
$missingOk = $compileShader.Invoke($null, @($missing))
Check (-not $missingOk -and $missing.Count -eq 2 -and ($missing.Errors | Where-Object { $_ -match "missing entry point" }).Count -eq 2) "missing entry point: CompileShader = $missingOk"
$badId = $bad.UploadToEngine()
Check ($badId -eq $invalidId) "UploadToEngine of a failed compilation: $badId"

$messages = @(Get-LogMessages | Select-Object -Skip $before)
$messages | ForEach-Object { Write-Host "    log: $(($_ -split "`n")[0])" }
Check (($messages | Where-Object { $_ -match "(?s)^\[Error\].*MainPS.*expected expression" }).Count -eq 1) "the DXC error is logged (LoggerView)"
Check (($messages | Where-Object { $_ -match "^\[Error\].*NoSuchFunction" }).Count -eq 2) "each failed variant is logged"
Check (($messages | Where-Object { $_ -match "^\[Error\].*MainPS" }).Count -ge 2) "the failed upload is logged"

Write-Host ""
if ($failures.Count -gt 0)
{
	Write-Host "FAILED ($($failures.Count))"
	exit 1
}
Write-Host "ALL PASSED"
exit 0
