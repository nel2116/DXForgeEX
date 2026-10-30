<#
.SYNOPSIS
    エディタのマテリアルのアセット(Material / AppliedMaterial)と既定のマテリアルを確かめる回帰テスト(Primal Day 077, Issue #30)。

.DESCRIPTION
    ビルド済みの DXForgeEditor.dll を読み込み、エディタと同じコードをリフレクションで呼ぶ。エディタのウィンドウは開かず、GPUも使わない。
    シェーダのコンパイルとエンジンへのシェーダの登録には、x64\<Configuration>\ の DXForgeEngineDLL.dll を使う
    (エンジンの初期化は要らない)。Logger と Material.Save() のアイコンに WPF が要るので、WPF の Application を作ってから呼ぶ。
    ファイルは %TEMP% の下に作り、最後に消す。

    確かめること:
      1. 既定のマテリアル: 既定のマテリアル用シェーダをコンパイルして DefaultAssets.CreateDefaultMaterial() で保存でき、
         読み戻すと同じシェーダと既定の値になる。同じシェーダならファイルを書き直さず、シェーダが変われば同じGUIDで作り直す。
      2. Material の Save → Load で、種類・モード・値・入力の名前・シェーダが同じになる。
      3. AppliedMaterial.PackForEngine() のバイト列が、エンジン(DXForgeEngineDLL/ResourceUpload.h)が読む並びと大きさになる
         (EngineUnitTests の Tests_ResourceUpload.cpp と同じ定数: 92 バイト、material_init_info は 4 バイト目から)。
      4. 同じ Material を使う2つの AppliedMaterial の1つがシェーダを外しても、もう1つが使うシェーダはエンジンに残る。
      5. EngineAPI.CreateResource / DestroyResource: エンジンを初期化せずに呼べる範囲(扱わない種類・シェーダのないマテリアル・
         空のデータ・無効なID)で、DLL の境界を通って無効なIDが返る(例外もassertも起きない)。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。

.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeEditor/Tests/material_asset_test.ps1
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
$editor = [System.Reflection.Assembly]::LoadFrom((Join-Path $OutDir "DXForgeEditor.dll"))

$flags = [System.Reflection.BindingFlags]"Public,NonPublic,Static,Instance"
$engineApi = $editor.GetType("DXForgeEditor.DllWrappers.EngineAPI", $true)
$defaultAssets = $editor.GetType("DXForgeEditor.Content.DefaultAssets", $true)
$assetType = $editor.GetType("DXForgeEditor.Content.Asset", $true)
$assetTypeEnum = $editor.GetType("DXForgeEditor.Content.AssetType", $true)
$shaderGroupType = $editor.GetType("DXForgeEditor.Content.ShaderGroup", $true)
$shaderType = $editor.GetType("DXForgeEditor.Content.ShaderType", $true)
$materialType = $editor.GetType("DXForgeEditor.Content.Material", $true)
$appliedMaterialType = $editor.GetType("DXForgeEditor.Content.AppliedMaterial", $true)
$materialTypeEnum = $editor.GetType("DXForgeEditor.Content.MaterialType", $true)
$materialModeEnum = $editor.GetType("DXForgeEditor.Content.MaterialMode", $true)
$uploadedAssetType = $editor.GetType("DXForgeEditor.Content.UploadedAsset", $true)

$compileShader = $engineApi.GetMethod("CompileShader", $flags, $null, [type[]]@($shaderGroupType), $null)
$createResource = $engineApi.GetMethod("CreateResource", $flags, $null, [type[]]@([byte[]], $assetTypeEnum), $null)
$destroyResource = $engineApi.GetMethod("DestroyResource", $flags, $null, [type[]]@([int], $assetTypeEnum), $null)
$createShaderGroup = $defaultAssets.GetMethod("CreateShaderGroup", $flags)
$getEngineShadersPath = $defaultAssets.GetMethod("GetEngineShadersPath", $flags)
$createDefaultMaterial = $defaultAssets.GetMethod("CreateDefaultMaterial", $flags)
$tryGetAssetInfo = $assetType.GetMethod("TryGetAssetInfo", $flags)
$uploadShaders = $appliedMaterialType.GetMethod("UploadShaders", $flags)
$unloadShaders = $appliedMaterialType.GetMethod("UnloadShaders", $flags)
$getContentId = $uploadedAssetType.GetMethod("GetContentId", $flags)
$resourceName = $defaultAssets.GetField("DefaultMaterialShadersResource", $flags).GetValue($null)
$uploadedShaderIds = $shaderGroupType.GetNestedType("UploadedShaderGroup", $flags).GetField("_uploadedShaderIds", $flags)
function Get-UploadedShaderCount { $uploadedShaderIds.GetValue($null).Count }

function New-ShaderGroup([string]$type, [string]$code, [string]$function, [string[]]$defines, [uint32[]]$keys)
{
	$createShaderGroup.Invoke($null, @([Enum]::Parse($shaderType, $type), $code, $function, $defines, $keys, $getEngineShadersPath.Invoke($null, @($RepoRoot + "\"))))
}

function Get-Packed($shaderGroup) { if ($shaderGroup) { [Convert]::ToBase64String($shaderGroup.PackForEngine()) } else { "" } }
function Get-Shader($material, [string]$type) { $material.GetShaderGroup([Enum]::Parse($shaderType, $type)) }

function Read-Material([string]$file)
{
	$material = [Activator]::CreateInstance($materialType)
	$loaded = $material.Load($file)
	@{ Loaded = $loaded; Material = $material }
}

$code = Get-Content (Join-Path $RepoRoot ("DXForgeEditor\" + $resourceName.Replace("/", "\"))) -Raw -Encoding utf8
$invalidId = -1
$work = Join-Path ([System.IO.Path]::GetTempPath()) ("dxforge_material_asset_test_" + [Guid]::NewGuid().ToString("N"))
$null = New-Item -ItemType Directory $work

try
{
	Write-Host "1. Default material (DXForgeEditor $Configuration)"
	$vs = New-ShaderGroup "Vertex" $code "MainVS" @("ELEMENTS_TYPE=0", "ELEMENTS_TYPE=1", "ELEMENTS_TYPE=3") @(0, 1, 3)
	$ps = New-ShaderGroup "Pixel" $code "MainPS" @("") @([uint32]::MaxValue)
	$compiled = $compileShader.Invoke($null, @($vs)) -and $compileShader.Invoke($null, @($ps))
	Check $compiled "the default material shaders compile"

	[string]$defaultFile = Join-Path $work "DefaultMaterial.asset"
	$created = $createDefaultMaterial.Invoke($null, @($defaultFile, $vs, $ps))
	$info = $tryGetAssetInfo.Invoke($null, @($defaultFile))
	Check ($created -and $info -and "$($info.Type)" -eq "Material" -and $info.Icon.Length -gt 0) "CreateDefaultMaterial saves a material asset: $created, $($info.Type), icon $($info.Icon.Length) bytes"
	$read = Read-Material $defaultFile
	$m = $read.Material
	Check ($read.Loaded -and "$($m.MaterialMode)" -eq "Default" -and "$($m.MaterialType)" -eq "Opaque" -and $m.Guid -eq $info.Guid) "it loads back: mode $($m.MaterialMode), type $($m.MaterialType)"
	Check ((Get-Packed (Get-Shader $m "Vertex")) -eq (Get-Packed $vs) -and (Get-Packed (Get-Shader $m "Pixel")) -eq (Get-Packed $ps) -and -not (Get-Shader $m "Compute")) "it has the compiled MainVS (3 variants) and MainPS"
	Check (($m.GetInputs() | ForEach-Object Name) -join "," -eq "Base Color,Emissive Color,Normal Map,Metallic and Roughness,Ambient Occlusion") "default inputs: $(($m.GetInputs() | ForEach-Object Name) -join ', ')"
	$s = $m.MaterialSurface
	Check ([Math]::Abs($s.BaseColor.ScR - 0.7) -lt 1e-6 -and [Math]::Abs($s.Roughness - 0.9) -lt 1e-6 -and $s.Metallic -eq 0 -and $s.EmissiveIntensity -eq 1) "default surface: base color $($s.BaseColor.ScR), roughness $($s.Roughness)"

	$writeTime = (Get-Item $defaultFile).LastWriteTimeUtc
	$again = $createDefaultMaterial.Invoke($null, @($defaultFile, $vs, $ps))
	Check ($again -and (Get-Item $defaultFile).LastWriteTimeUtc -eq $writeTime) "the same shaders do not rewrite the file"
	$ps0 = New-ShaderGroup "Pixel" $code "MainPS" @("") @(0)
	[void]$compileShader.Invoke($null, @($ps0))
	$recreated = $createDefaultMaterial.Invoke($null, @($defaultFile, $vs, $ps0))
	$read = Read-Material $defaultFile
	Check ($recreated -and $read.Material.Guid -eq $info.Guid -and (Get-Shader $read.Material "Pixel").Keys[0] -eq 0) "changed shaders recreate it with the same GUID"

	Write-Host "2. Material Save / Load"
	$material = [Activator]::CreateInstance($materialType)
	$material.MaterialMode = [Enum]::Parse($materialModeEnum, "Default")
	$material.MaterialType = [Enum]::Parse($materialTypeEnum, "Opaque")
	[void]$material.AddShaderGroup($vs)
	[void]$material.AddShaderGroup($ps)
	$surface = $material.MaterialSurface
	$surface.BaseColor = [System.Windows.Media.Color]::FromScRgb(0.5, 0.1, 0.2, 0.3)
	$surface.EmissiveColor = [System.Windows.Media.Color]::FromScRgb(1, 0.4, 0.5, 0.6)
	$surface.EmissiveIntensity = 2.5
	$surface.Metallic = 0.25
	$surface.Roughness = 0.75
	$material.GetInputs()[2].Name = "Normal"
	[string]$file = Join-Path $work "Material.asset"
	$saved = @($material.Save($file))
	$read = Read-Material $file
	$m = $read.Material
	$s = $m.MaterialSurface
	Check ($saved.Count -eq 1 -and $read.Loaded -and $m.Guid -eq $material.Guid) "saved and loaded: $($saved -join ', ')"
	Check ($s.BaseColor -eq $surface.BaseColor -and $s.EmissiveColor.ScR -eq $surface.EmissiveColor.ScR -and $s.EmissiveColor.ScB -eq $surface.EmissiveColor.ScB -and
		$s.EmissiveIntensity -eq 2.5 -and $s.Metallic -eq 0.25 -and $s.Roughness -eq 0.75) "the surface round trips: $($s.BaseColor), $($s.EmissiveIntensity), $($s.Metallic), $($s.Roughness)"
	Check ($m.GetInputs()[2].Name -eq "Normal" -and "$($m.MaterialMode)" -eq "Default" -and "$($m.MaterialType)" -eq "Opaque") "the mode, type and input names round trip"
	$mvs = Get-Shader $m "Vertex"
	Check ((Get-Packed $mvs) -eq (Get-Packed $vs) -and $mvs.Code -eq $vs.Code -and $mvs.FunctionName -eq "MainVS" -and ($mvs.ExtraArgs[1] -join " ") -eq ($vs.ExtraArgs[1] -join " ")) "the shader groups round trip"

	Write-Host "3. AppliedMaterial.PackForEngine"
	$materialInfo = $tryGetAssetInfo.Invoke($null, @($file))
	$applied = [Activator]::CreateInstance($appliedMaterialType, @($materialInfo))
	$uploadShaders.Invoke($applied, $null)
	$appliedSource = $appliedMaterialType.GetField("_material", $flags).GetValue($applied)
	$vsId = (Get-Shader $appliedSource "Vertex").ContentId
	$data = $applied.PackForEngine()
	# EngineUnitTests の Tests_ResourceUpload.cpp と同じ定数: packed_material_size(0) = 92、material_init_info は 4 バイト目から
	# (texture_ids +0、surface +8、type +48、texture_count +52、shader_ids +56)
	Check ($data.Length -eq 92) "92 bytes without textures: $($data.Length)"
	$f = { param($offset) [BitConverter]::ToSingle($data, $offset) }
	$i = { param($offset) [BitConverter]::ToInt32($data, $offset) }
	Check ((& $i 0) -eq 0 -and [BitConverter]::ToInt64($data, 4) -eq 0) "texture count 0 and a null texture_ids pointer"
	Check ((& $f 12) -eq $surface.BaseColor.ScR -and (& $f 24) -eq $surface.BaseColor.ScA -and (& $f 28) -eq $surface.EmissiveColor.ScR -and
		(& $f 40) -eq 2.5 -and (& $f 44) -eq 0.25 -and (& $f 48) -eq 0.75) "material_surface at +12: $((12..48 | Where-Object { $_ % 4 -eq 0 } | ForEach-Object { & $f $_ }) -join ', ')"
	Check ((& $i 52) -eq 0 -and (& $i 56) -eq 0) "type (opaque) at +52 and texture count at +56"
	$shaderIds = 0..7 | ForEach-Object { & $i (60 + 4 * $_) }
	Check ($vsId -ne $invalidId -and $shaderIds[0] -eq $vsId -and $shaderIds[4] -ne $invalidId -and $shaderIds[4] -ne $vsId -and
		($shaderIds[1, 2, 3, 5, 6, 7] | Where-Object { $_ -ne $invalidId }).Count -eq 0) "shader ids at +60 (vertex, ..., pixel, ...): $($shaderIds -join ', ')"

	$applied.Inputs[0].Asset = [Activator]::CreateInstance($materialInfo.GetType())
	$applied.Inputs[0].Asset.Guid = [Guid]::NewGuid()
	$applied.Inputs[0].Asset.FullPath = "C:\NotUploaded.asset"
	Check ($applied.PackForEngine().Length -eq 0) "an input that is not uploaded gives no data"
	$applied.Inputs[0].Asset = $null
	$unloadShaders.Invoke($applied, $null)
	Check ((Get-UploadedShaderCount) -eq 0 -and (Get-Shader $appliedSource "Vertex").ContentId -eq $invalidId) "UnloadShaders removes the shaders: $(Get-UploadedShaderCount) left"

	Write-Host "4. Two applied materials sharing a material"
	$a1 = [Activator]::CreateInstance($appliedMaterialType, @($materialInfo))
	$a2 = [Activator]::CreateInstance($appliedMaterialType, @($materialInfo))
	$uploadShaders.Invoke($a1, $null)
	$uploadShaders.Invoke($a2, $null)
	$count = Get-UploadedShaderCount
	$p1 = [Convert]::ToBase64String($a1.PackForEngine())
	$p2 = [Convert]::ToBase64String($a2.PackForEngine())
	Check ($count -eq 2 -and $p1 -eq $p2) "both use the same uploaded shaders: $count shader groups"
	$unloadShaders.Invoke($a1, $null)
	$sharedVs = $a2.GetType().GetField("_material", $flags).GetValue($a2).GetShaderGroup([Enum]::Parse($shaderType, "Vertex"))
	Check ((Get-UploadedShaderCount) -eq 2 -and $sharedVs.ContentId -ne $invalidId -and [Convert]::ToBase64String($a2.PackForEngine()) -eq $p2) "removing one keeps the shaders of the other: $(Get-UploadedShaderCount) shader groups, vertex id $($sharedVs.ContentId)"
	$unloadShaders.Invoke($a2, $null)
	Check ((Get-UploadedShaderCount) -eq 0 -and $sharedVs.ContentId -eq $invalidId) "removing both removes the shaders: $(Get-UploadedShaderCount) left"
	$uploadShaders.Invoke($a1, $null)
	Check ((Get-UploadedShaderCount) -eq 2) "they can be uploaded again: $(Get-UploadedShaderCount) shader groups"
	$unloadShaders.Invoke($a1, $null)

	Write-Host "5. EngineAPI.CreateResource / DestroyResource without the engine initialized"
	$animation = [Enum]::Parse($assetTypeEnum, "Animation")
	$materialAsset = [Enum]::Parse($assetTypeEnum, "Material")
	$mesh = [Enum]::Parse($assetTypeEnum, "Mesh")
	Check ($createResource.Invoke($null, @([byte[]]@(1, 2, 3, 4), $animation)) -eq $invalidId) "an unsupported type is rejected by the DLL"
	$noShaders = [byte[]]::new(92)
	for ($k = 0; $k -lt 8; $k++) { [Array]::Copy([BitConverter]::GetBytes(-1), 0, $noShaders, 60 + 4 * $k, 4) }
	Check ($createResource.Invoke($null, @($noShaders, $materialAsset)) -eq $invalidId) "a material without shaders is rejected by the DLL"
	Check ($createResource.Invoke($null, @([byte[]]@(), $mesh)) -eq $invalidId) "empty data is not passed to the DLL"
	$destroyResource.Invoke($null, @($invalidId, $mesh))
	$destroyResource.Invoke($null, @(0, $animation))
	Check $true "DestroyResource with an invalid id or an unsupported type does nothing"
	Check ($getContentId.Invoke($null, @([Guid]::NewGuid())) -eq $invalidId) "UploadedAsset.GetContentId of an asset that is not uploaded"
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
