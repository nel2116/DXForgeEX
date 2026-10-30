<#
.SYNOPSIS
    エディタのマテリアルのアセット(Material / AppliedMaterial)と既定のマテリアルを確かめる回帰テスト(Primal Day 077, Issue #30)。

.DESCRIPTION
    ビルド済みの DXForgeEditor.dll を読み込み、エディタと同じコードをリフレクションで呼ぶ。エディタのウィンドウは開かず、GPUも使わない。
    シェーダのコンパイルとエンジンへのシェーダの登録には、x64\<Configuration>\ の DXForgeEngineDLL.dll を使う
    (エンジンの初期化は要らない)。DLL がなければ、コンパイル済みに見せた偽のシェーダーグループと、偽のシェーダーの登録
    (ShaderGroup.AddShaderGroupToEngine / RemoveShaderGroupFromEngine)を使い、DLL が要る項目は
    SKIP として名前と件数を出す(CI のランナーには ATL がなく DLL をビルドできないため)。-RequireEngineDll を付けると、
    DLL がないときは失敗にする(ローカルでは付けて、全項目を実行する)。Logger と Material.Save() のアイコンに WPF が要るので、WPF の Application を作ってから呼ぶ。
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
      6. Codex の指摘(#145): エンジンのリソースの作成・破棄とアセットの読み込みを偽物に差し替え(UploadedAsset の
         LoadAsset / CreateResource / DestroyResource)、エンジンを初期化せずに次を確かめる。
         - 入力のテクスチャがまだアップロードされていなくても、AppliedMaterial.UploadToEngine() が先にアップロードし、
           マテリアルのデータにそのIDが入る。外すとテクスチャも破棄される。
         - AppliedMaterial.Clone() が入力のアセットを複製する。
         - 入力を減らしたり増やしたりした Material の Save → Load で、入力の名前が同じになる。
         - Material を保存し直したら、そのあとに作る AppliedMaterial は新しい内容を使う。参照数が0になったら表から消える。
      7. Codex の2回目の指摘(#145)と、アップロード・読み込みの経路の失敗の扱い: シェーダーの登録も偽物に差し替え
         (ShaderGroup の AddShaderGroupToEngine / RemoveShaderGroupFromEngine)、次を確かめる。
         - シェーダーグループの登録に失敗したら、マテリアルのアップロードも失敗し、登録できたシェーダーは外れる。
         - 途中までしか読めない既定のマテリアルは、シェーダーが同じでも作り直す。
         - 同じ内容の2つの適用マテリアルの1つを外し、内容を変えてアップロードし直せる(もう1つはそのまま)。
           アップロードしていない適用マテリアルを外しても何も起きない。
         - 読み込めないアセット(ファイルがない、壊れている、種類が違う)はアップロードしない(リソースを作らない)。
      8. AppliedMaterial の状態の遷移(Material.cs の AppliedMaterial の状態の表): 操作を組み合わせて実行し、操作のたびに
         持っているもの(アップロードしたマテリアルとその参照数、シェーダーの参照数、パックの結果)と、エンジンのリソースを
         作った・破棄した回数を確かめる。
      DLL が要る項目: 1 の実際のコンパイル、5 の DLL を呼ぶ項目。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。

.PARAMETER RequireEngineDll
    DXForgeEngineDLL.dll がなければ失敗にする(DLL が要る項目を SKIP にしない)。

.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeEditor/Tests/material_asset_test.ps1 -RequireEngineDll
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

$skipped = [System.Collections.Generic.List[string]]::new()
function Skip([string]$name)
{
	Write-Host "  SKIP $name (DXForgeEngineDLL.dll がない)"
	$skipped.Add($name)
}

if ([System.Threading.Thread]::CurrentThread.GetApartmentState() -ne "STA") { throw "Run this script on an STA thread (pwsh -STA)." }

Add-Type -AssemblyName PresentationFramework
$null = [System.Windows.Application]::new()
$engineDllPath = Join-Path $OutDir "DXForgeEngineDLL.dll"
$hasEngineDll = Test-Path $engineDllPath
if (-not $hasEngineDll)
{
	if ($RequireEngineDll) { throw "$engineDllPath がありません(./build.ps1 -Target native -Configuration $Configuration -Project DXForgeEngineDLL)。" }
	Write-Host "NOTE: $engineDllPath がないため、DLL が要る項目は SKIP する。"
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
$acquireShaders = $appliedMaterialType.GetMethod("AcquireShaders", $flags)
$releaseShaders = $appliedMaterialType.GetMethod("ReleaseShaders", $flags)
$packMaterial = $appliedMaterialType.GetMethod("Pack", $flags)
# AppliedMaterial のシェーダーの参照を1つ増やし、シェーダーのIDの一覧を返す(失敗したら $null)
function Invoke-AcquireShaders($applied)
{
	$arguments = [object[]]@($null)
	if ($acquireShaders.Invoke($applied, $arguments)) { return , $arguments[0] }
	return $null
}
# そのシェーダーのIDでパックする(PackForEngine() はアップロードしている間だけデータを返すため)
function Invoke-Pack($applied, $shaderIds) { return , [byte[]]$packMaterial.Invoke($applied, @(, $shaderIds)) }
$getContentId = $uploadedAssetType.GetMethod("GetContentId", $flags)
$resourceName = $defaultAssets.GetField("DefaultMaterialShadersResource", $flags).GetValue($null)
$uploadedShaderIds = $shaderGroupType.GetNestedType("UploadedShaderGroup", $flags).GetField("_uploadedShaderIds", $flags)
function Get-UploadedShaderCount { $uploadedShaderIds.GetValue($null).Count }

function New-ShaderGroup([string]$type, [string]$code, [string]$function, [string[]]$defines, [uint32[]]$keys)
{
	$createShaderGroup.Invoke($null, @([Enum]::Parse($shaderType, $type), $code, $function, $defines, $keys, $getEngineShadersPath.Invoke($null, @($RepoRoot + "\"))))
}

# DLL がないときに使う、コンパイル済みに見せたシェーダーグループ(バイトコードとハッシュは偽物)
function Set-FakeCompiled($shaderGroup, [int]$seed)
{
	$shaderGroup.ByteCode.Clear(); $shaderGroup.Hash.Clear(); $shaderGroup.Errors.Clear(); $shaderGroup.Assembly.Clear()
	for ($k = 0; $k -lt $shaderGroup.Keys.Count; $k++)
	{
		$shaderGroup.ByteCode.Add([byte[]](1..(16 + $k) | ForEach-Object { [byte](($_ + $seed) % 256) }))
		$shaderGroup.Hash.Add([byte[]](1..16 | ForEach-Object { [byte](($_ * 7 + $k + $seed) % 256) }))
		$shaderGroup.Errors.Add("")
		$shaderGroup.Assembly.Add("fake")
	}
}

function Compile-ShaderGroup($shaderGroup, [int]$seed)
{
	if ($hasEngineDll) { return [bool]$compileShader.Invoke($null, @($shaderGroup)) }
	Set-FakeCompiled $shaderGroup $seed
	return $shaderGroup.IsCompiled
}

# DLL がないときは、シェーダーの登録を偽物にする(IDを順に返す)
if (-not $hasEngineDll)
{
	$script:noDllShaderId = 0
	$shaderGroupType.GetProperty("AddShaderGroupToEngine", $flags).SetValue($null, [System.Management.Automation.LanguagePrimitives]::ConvertTo(
		{ param($g) $id = $script:noDllShaderId; $script:noDllShaderId++; [int]$id }, [System.Func`2].MakeGenericType($shaderGroupType, [int])))
	$shaderGroupType.GetProperty("RemoveShaderGroupFromEngine", $flags).SetValue($null, [System.Management.Automation.LanguagePrimitives]::ConvertTo(
		{ param([int]$id) }, [System.Action`1].MakeGenericType([int])))
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
	$compiled = (Compile-ShaderGroup $vs 1) -and (Compile-ShaderGroup $ps 2)
	if ($hasEngineDll) { Check $compiled "the default material shaders compile" }
	else { Skip "the default material shaders compile"; Check $compiled "fake compiled shader groups" }

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
	[void](Compile-ShaderGroup $ps0 2)
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
	$appliedIds = Invoke-AcquireShaders $applied
	$appliedSource = $appliedMaterialType.GetProperty("_material", $flags).GetValue($applied)
	$vsId = (Get-Shader $appliedSource "Vertex").ContentId
	$data = Invoke-Pack $applied $appliedIds
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
	Check ((Invoke-Pack $applied $appliedIds).Length -eq 0) "an input that is not uploaded gives no data"
	$applied.Inputs[0].Asset = $null
	Check ($applied.PackForEngine().Length -eq 0) "PackForEngine gives no data while the applied material is not uploaded"
	$releaseShaders.Invoke($applied, $null)
	Check ((Get-UploadedShaderCount) -eq 0 -and (Get-Shader $appliedSource "Vertex").ContentId -eq $invalidId) "ReleaseShaders removes the shaders: $(Get-UploadedShaderCount) left"

	Write-Host "4. Two applied materials sharing a material"
	$a1 = [Activator]::CreateInstance($appliedMaterialType, @($materialInfo))
	$a2 = [Activator]::CreateInstance($appliedMaterialType, @($materialInfo))
	$ids1 = Invoke-AcquireShaders $a1
	$ids2 = Invoke-AcquireShaders $a2
	$count = Get-UploadedShaderCount
	$p1 = [Convert]::ToBase64String((Invoke-Pack $a1 $ids1))
	$p2 = [Convert]::ToBase64String((Invoke-Pack $a2 $ids2))
	Check ($count -eq 2 -and $p1 -eq $p2) "both use the same uploaded shaders: $count shader groups"
	$releaseShaders.Invoke($a1, $null)
	$sharedVs = $appliedMaterialType.GetProperty("_material", $flags).GetValue($a2).GetShaderGroup([Enum]::Parse($shaderType, "Vertex"))
	Check ((Get-UploadedShaderCount) -eq 2 -and $sharedVs.ContentId -ne $invalidId -and [Convert]::ToBase64String((Invoke-Pack $a2 $ids2)) -eq $p2) "removing one keeps the shaders of the other: $(Get-UploadedShaderCount) shader groups, vertex id $($sharedVs.ContentId)"
	$releaseShaders.Invoke($a2, $null)
	Check ((Get-UploadedShaderCount) -eq 0 -and $sharedVs.ContentId -eq $invalidId) "removing both removes the shaders: $(Get-UploadedShaderCount) left"
	[void](Invoke-AcquireShaders $a1)
	Check ((Get-UploadedShaderCount) -eq 2) "they can be uploaded again: $(Get-UploadedShaderCount) shader groups"
	$releaseShaders.Invoke($a1, $null)

	Write-Host "5. EngineAPI.CreateResource / DestroyResource without the engine initialized"
	$animation = [Enum]::Parse($assetTypeEnum, "Animation")
	$materialAsset = [Enum]::Parse($assetTypeEnum, "Material")
	$mesh = [Enum]::Parse($assetTypeEnum, "Mesh")
	if ($hasEngineDll)
	{
		Check ($createResource.Invoke($null, @([byte[]]@(1, 2, 3, 4), $animation)) -eq $invalidId) "an unsupported type is rejected by the DLL"
		$noShaders = [byte[]]::new(92)
		for ($k = 0; $k -lt 8; $k++) { [Array]::Copy([BitConverter]::GetBytes(-1), 0, $noShaders, 60 + 4 * $k, 4) }
		Check ($createResource.Invoke($null, @($noShaders, $materialAsset)) -eq $invalidId) "a material without shaders is rejected by the DLL"
	}
	else
	{
		Skip "an unsupported type is rejected by the DLL"
		Skip "a material without shaders is rejected by the DLL"
	}
	Check ($createResource.Invoke($null, @([byte[]]@(), $mesh)) -eq $invalidId) "empty data is not passed to the DLL"
	if ($hasEngineDll)
	{
		$destroyResource.Invoke($null, @($invalidId, $mesh))
		$destroyResource.Invoke($null, @(0, $animation))
		Check $true "DestroyResource with an invalid id or an unsupported type does nothing"
	}
	else { Skip "DestroyResource with an invalid id or an unsupported type does nothing" }
	Check ($getContentId.Invoke($null, @([Guid]::NewGuid())) -eq $invalidId) "UploadedAsset.GetContentId of an asset that is not uploaded"

	Write-Host "6. Codex review (#145)"
	$assetInfoType = $materialInfo.GetType()
	$textureType = $editor.GetType("DXForgeEditor.Content.Texture", $true)
	$sliceType = $editor.GetType("DXForgeEditor.Content.Slice", $true)
	$materialInputType = $editor.GetType("DXForgeEditor.Content.MaterialInput", $true)
	$uploadedAssets = $uploadedAssetType.GetField("_uploadedAssets", $flags).GetValue($null)
	$loadedMaterials = $appliedMaterialType.GetField("_loadedMaterials", $flags).GetValue($null)
	$textureAsset = [Enum]::Parse($assetTypeEnum, "Texture")

	# 偽のエンジン: リソースのIDを順に返し、作ったデータと破棄したIDを覚える
	$script:fake = @{ Next = 100; Created = @{}; Data = @{}; Destroyed = [System.Collections.Generic.List[int]]::new(); Textures = @{} }
	$createSb = { param([byte[]]$data, $type) if ($script:fake.Fail) { return [int]-1 }; $id = $script:fake.Next; $script:fake.Next++; $script:fake.Created[$id] = "$type"; $script:fake.Data[$id] = $data; [int]$id }
	$destroySb = { param([int]$id, $type) $script:fake.Destroyed.Add($id) }
	$loadSb = { param($info) $script:fake.Textures[$info.Guid] }
	$seams = @{
		CreateResource = [System.Func`3].MakeGenericType([byte[]], $assetTypeEnum, [int])
		DestroyResource = [System.Action`2].MakeGenericType([int], $assetTypeEnum)
		LoadAsset = [System.Func`2].MakeGenericType($assetInfoType, $assetType)
	}
	$originals = @{}
	foreach ($name in $seams.Keys) { $originals[$name] = $uploadedAssetType.GetProperty($name, $flags).GetValue($null) }
	$uploadedAssetType.GetProperty("CreateResource", $flags).SetValue($null, [System.Management.Automation.LanguagePrimitives]::ConvertTo($createSb, $seams.CreateResource))
	$uploadedAssetType.GetProperty("DestroyResource", $flags).SetValue($null, [System.Management.Automation.LanguagePrimitives]::ConvertTo($destroySb, $seams.DestroyResource))
	$uploadedAssetType.GetProperty("LoadAsset", $flags).SetValue($null, [System.Management.Automation.LanguagePrimitives]::ConvertTo($loadSb, $seams.LoadAsset))

	try
	{
		# 4x4 の R8G8B8A8 のテクスチャ(Texture.PackForEngine() で詰める)
		$texInfo = [Activator]::CreateInstance($assetInfoType)
		$texInfo.Type = $textureAsset
		$texInfo.Guid = [Guid]::NewGuid()
		$texInfo.FullPath = Join-Path $work "Texture.asset"
		$texture = [Activator]::CreateInstance($textureType)
		$assetType.GetProperty("Guid", $flags).SetValue($texture, $texInfo.Guid)
		$texture.Width = 4; $texture.Height = 4; $texture.ArraySize = 1; $texture.MipLevels = 1
		$texture.Format = [Enum]::ToObject($texture.Format.GetType(), 28)
		$slice = [Activator]::CreateInstance($sliceType)
		$slice.Width = 4; $slice.Height = 4; $slice.RowPitch = 16; $slice.SlicePitch = 64; $slice.RawContent = [byte[]]::new(64)
		$mips = [Activator]::CreateInstance([System.Collections.Generic.List`1].MakeGenericType($sliceType)); $mips.Add($slice)
		$array = [Activator]::CreateInstance([System.Collections.Generic.List`1].MakeGenericType($mips.GetType())); $array.Add($mips)
		$slices = [Activator]::CreateInstance([System.Collections.Generic.List`1].MakeGenericType($array.GetType())); $slices.Add($array)
		$textureType.GetProperty("Slices", $flags).SetValue($texture, $slices)
		$script:fake.Textures[$texInfo.Guid] = $texture

		# 1. 入力のテクスチャを先にアップロードする
		$withTexture = [Activator]::CreateInstance($appliedMaterialType, @($materialInfo))
		$withTexture.Inputs[0].Asset = $texInfo
		$uploaded = $withTexture.UploadToEngine()
		$texId = $getContentId.Invoke($null, @($texInfo.Guid))
		$materialId = if ($withTexture.UploadedAsset) { $withTexture.UploadedAsset.ContentId } else { $invalidId }
		$materialData = if ($script:fake.Data.ContainsKey($materialId)) { $script:fake.Data[$materialId] } else { [byte[]]@() }
		Check ($uploaded -and $texId -ne $invalidId -and $script:fake.Created[$texId] -eq "Texture" -and $uploadedAssets[$texInfo.Guid].ReferenceCount -eq 1) "UploadToEngine uploads the input texture first: $uploaded, texture id $texId, references $(if ($uploadedAssets.ContainsKey($texInfo.Guid)) { $uploadedAssets[$texInfo.Guid].ReferenceCount } else { 0 })"
		Check ($materialData.Length -eq 96 -and [BitConverter]::ToInt32($materialData, 0) -eq 1 -and [BitConverter]::ToInt32($materialData, 4) -eq $texId) "the material data has the texture id: $($materialData.Length) bytes"
		if ($uploaded)
		{
			$withTexture.UnloadFromEngine()
			Check ($getContentId.Invoke($null, @($texInfo.Guid)) -eq $invalidId -and $script:fake.Destroyed.Contains($texId) -and $script:fake.Destroyed.Contains($materialId) -and $uploadedAssets.Count -eq 0) "UnloadFromEngine destroys the material and the texture: destroyed $($script:fake.Destroyed -join ', ')"
		}
		Check ((Get-UploadedShaderCount) -eq 0) "no shaders are left uploaded: $(Get-UploadedShaderCount)"

		# 2. Clone() が入力のアセットを複製する
		$original = [Activator]::CreateInstance($appliedMaterialType, @($materialInfo))
		$original.Inputs[1].Asset = $texInfo
		$clone = $original.Clone()
		$cloneAssets = @($clone.GetReferencedAssets())
		Check ($cloneAssets.Count -eq 1 -and $cloneAssets[0].Guid -eq $texInfo.Guid -and $clone.Inputs[1].Asset -eq $texInfo -and -not [object]::ReferenceEquals($clone.Inputs[1], $original.Inputs[1])) "Clone copies the input assets: $($cloneAssets.Count)"

		# 3. 入力の数を保存する
		$inputs = [Activator]::CreateInstance($materialType)
		$inputs.MaterialMode = [Enum]::Parse($materialModeEnum, "Default")
		$inputs.DefaultMaterialInputs.RemoveInput("Normal Map")
		$inputs.DefaultMaterialInputs.AddInput([Activator]::CreateInstance($materialInputType, @("Custom 1")))
		$inputs.DefaultMaterialInputs.AddInput([Activator]::CreateInstance($materialInputType, @("Custom 2")))
		[string]$inputsFile = Join-Path $work "Inputs.asset"
		[void]$inputs.Save($inputsFile)
		$read = Read-Material $inputsFile
		$expected = ($inputs.GetInputs() | ForEach-Object Name) -join ","
		$actual = if ($read.Loaded) { ($read.Material.GetInputs() | ForEach-Object Name) -join "," } else { "(not loaded)" }
		Check ($read.Loaded -and $actual -eq $expected) "added and removed inputs round trip: $actual"

		# 4. 保存し直した Material を使う。参照数が0になったら表から消える
		$cached = [Activator]::CreateInstance($materialType)
		$cached.MaterialMode = [Enum]::Parse($materialModeEnum, "Default")
		[void]$cached.AddShaderGroup($vs)
		[void]$cached.AddShaderGroup($ps)
		$cached.MaterialSurface.Roughness = 0.75
		[string]$cachedFile = Join-Path $work "Cached.asset"
		[void]$cached.Save($cachedFile)
		$first = [Activator]::CreateInstance($appliedMaterialType, @($tryGetAssetInfo.Invoke($null, @($cachedFile))))
		$cached.MaterialSurface.Roughness = 0.5
		[void]$cached.Save($cachedFile)
		$second = [Activator]::CreateInstance($appliedMaterialType, @($tryGetAssetInfo.Invoke($null, @($cachedFile))))
		Check ($first.MaterialSurface.Roughness -eq 0.75 -and $second.MaterialSurface.Roughness -eq 0.5) "an applied material made after saving uses the saved material: $($first.MaterialSurface.Roughness) -> $($second.MaterialSurface.Roughness)"
		$secondUploaded = $second.UploadToEngine()
		if ($secondUploaded) { $second.UnloadFromEngine() }
		Check ($secondUploaded -and -not $loadedMaterials.ContainsKey($cached.Guid) -and (Get-UploadedShaderCount) -eq 0) "the loaded material is dropped when no applied material uses it: uploaded $secondUploaded, cached $($loadedMaterials.ContainsKey($cached.Guid))"

		Write-Host "7. Failures on the upload and load paths (#145)"
		$script:fakeShaders = @{ Next = 500; Live = [System.Collections.Generic.HashSet[int]]::new(); FailType = "" }
		$addShaderSb = { param($g) if ("$($g.Type)" -eq $script:fakeShaders.FailType) { return [int]-1 }; $id = $script:fakeShaders.Next; $script:fakeShaders.Next++; [void]$script:fakeShaders.Live.Add($id); [int]$id }
		$removeShaderSb = { param([int]$id) [void]$script:fakeShaders.Live.Remove($id) }
		$shaderSeams = @{
			AddShaderGroupToEngine = [System.Func`2].MakeGenericType($shaderGroupType, [int])
			RemoveShaderGroupFromEngine = [System.Action`1].MakeGenericType([int])
		}
		$shaderOriginals = @{}
		foreach ($name in $shaderSeams.Keys) { $shaderOriginals[$name] = $shaderGroupType.GetProperty($name, $flags).GetValue($null) }
		$shaderGroupType.GetProperty("AddShaderGroupToEngine", $flags).SetValue($null, [System.Management.Automation.LanguagePrimitives]::ConvertTo($addShaderSb, $shaderSeams.AddShaderGroupToEngine))
		$shaderGroupType.GetProperty("RemoveShaderGroupFromEngine", $flags).SetValue($null, [System.Management.Automation.LanguagePrimitives]::ConvertTo($removeShaderSb, $shaderSeams.RemoveShaderGroupFromEngine))
		$addToScene = $uploadedAssetType.GetMethod("AddToScene", $flags)
		try
		{
			# シェーダーグループの登録に失敗したら、マテリアルのアップロードも失敗する
			$script:fakeShaders.FailType = "Pixel"
			$failing = [Activator]::CreateInstance($appliedMaterialType, @($materialInfo))
			$createdBefore = $script:fake.Created.Count
			$failedUpload = $failing.UploadToEngine()
			Check (-not $failedUpload -and $script:fake.Created.Count -eq $createdBefore -and $script:fakeShaders.Live.Count -eq 0 -and (Get-UploadedShaderCount) -eq 0) "a failed shader upload fails the material upload and releases the other shaders: uploaded $failedUpload, shaders left $($script:fakeShaders.Live.Count)"
			if ($failedUpload) { $failing.UnloadFromEngine() }
			$script:fakeShaders.FailType = ""

			# 途中までしか読めない既定のマテリアルは作り直す
			[string]$partialFile = Join-Path $work "Partial.asset"
			[void]$createDefaultMaterial.Invoke($null, @($partialFile, $vs, $ps))
			$bytes = [System.IO.File]::ReadAllBytes($partialFile)
			[System.IO.File]::WriteAllBytes($partialFile, $bytes[0..($bytes.Length - 9)])
			$broken = Read-Material $partialFile
			$recreatedPartial = $createDefaultMaterial.Invoke($null, @($partialFile, $vs, $ps))
			$read = Read-Material $partialFile
			Check (-not $broken.Loaded -and $recreatedPartial -and $read.Loaded) "a default material that only partly loads is recreated: broken loads $($broken.Loaded), recreated $recreatedPartial, loads $($read.Loaded)"

			# 同じ内容の適用マテリアルの1つを外して、変えてアップロードし直す
			$sharedA = [Activator]::CreateInstance($appliedMaterialType, @($materialInfo))
			$sharedB = [Activator]::CreateInstance($appliedMaterialType, @($materialInfo))
			$uploadedA = $sharedA.UploadToEngine()
			$uploadedB = $sharedB.UploadToEngine()
			Check ($uploadedA -and $uploadedB -and [object]::ReferenceEquals($sharedA.UploadedAsset, $sharedB.UploadedAsset) -and $sharedA.UploadedAsset.ReferenceCount -eq 2) "two applied materials with the same content share one engine material"
			$sharedA.UnloadFromEngine()
			$sharedA.MaterialSurface.Roughness = 0.3
			$reuploadedA = $sharedA.UploadToEngine()
			$idA = $getContentId.Invoke($null, @($sharedA.Guid))
			$idB = $getContentId.Invoke($null, @($sharedB.Guid))
			Check ($reuploadedA -and -not [object]::ReferenceEquals($sharedA.UploadedAsset, $sharedB.UploadedAsset) -and $idA -ne $invalidId -and $idB -ne $invalidId -and $idA -ne $idB) "the one that was unloaded can be changed and uploaded again: ids $idA, $idB"
			$sharedA.UnloadFromEngine()
			$sharedA.UnloadFromEngine()
			$sharedB.UnloadFromEngine()
			Check ($uploadedAssets.Count -eq 0 -and $script:fakeShaders.Live.Count -eq 0 -and -not $loadedMaterials.ContainsKey($materialInfo.Guid)) "unloading twice does nothing, and everything is released: $($uploadedAssets.Count) assets, $($script:fakeShaders.Live.Count) shaders"

			Write-Host "8. AppliedMaterial state transitions"
			$script:fake.Fail = $false
			$loadedMaterialField = $appliedMaterialType.GetField("_loadedMaterial", $flags)
			# 操作のあとの状態: 期待する値と比べる
			#   Uploaded: アップロードしているか / Refs: そのマテリアルの参照数 / Shaders: このマテリアルのシェーダーの参照数
			#   Pack: PackForEngine() の長さ / Created, Destroyed: このシナリオでエンジンのマテリアルを作った・破棄した回数
			function Get-MaterialCounts
			{
				$created = @($script:fake.Created.Values | Where-Object { $_ -eq "Material" }).Count
				$destroyed = @($script:fake.Destroyed | Where-Object { $script:fake.Created[$_] -eq "Material" }).Count
				@{ Created = $created; Destroyed = $destroyed }
			}
			function Assert-State([string]$step, $am, [hashtable]$expected, [hashtable]$start)
			{
				$counts = Get-MaterialCounts
				$actual = [ordered]@{
					Uploaded = $null -ne $am.UploadedAsset
					Refs = if ($am.UploadedAsset) { $am.UploadedAsset.ReferenceCount } else { 0 }
					Shaders = $loadedMaterialField.GetValue($am).ReferenceCount
					Pack = $am.PackForEngine().Length
					Created = $counts.Created - $start.Created
					Destroyed = $counts.Destroyed - $start.Destroyed
				}
				$ok = $true
				foreach ($key in $expected.Keys) { if ("$($actual[$key])" -ne "$($expected[$key])") { $ok = $false } }
				$text = ($actual.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join " "
				Check $ok "${step}: $text"
			}
			function New-Applied { [Activator]::CreateInstance($appliedMaterialType, @($materialInfo)) }

			# S1: 同じ内容で2回アップロードしてもパックできる(Codexの指摘3回目)
			$start = Get-MaterialCounts
			$s1 = New-Applied
			[void]$s1.UploadToEngine()
			Assert-State "S1 upload" $s1 @{ Uploaded = $true; Refs = 1; Shaders = 1; Pack = 92; Created = 1; Destroyed = 0 } $start
			$again = $s1.UploadToEngine()
			Assert-State "S1 upload again (same content, returns $again)" $s1 @{ Uploaded = $true; Refs = 1; Shaders = 1; Pack = 92; Created = 1; Destroyed = 0 } $start
			$s1.UnloadFromEngine()
			Assert-State "S1 unload" $s1 @{ Uploaded = $false; Refs = 0; Shaders = 0; Pack = 0; Created = 1; Destroyed = 1 } $start
			$s1.UnloadFromEngine()
			Assert-State "S1 unload again (not uploaded)" $s1 @{ Uploaded = $false; Refs = 0; Shaders = 0; Pack = 0; Created = 1; Destroyed = 1 } $start

			# S2: 内容を変えてアップロードし直すと、前のマテリアルは破棄される
			$start = Get-MaterialCounts
			$s2 = New-Applied
			[void]$s2.UploadToEngine()
			$s2.MaterialSurface.Roughness = 0.2
			[void]$s2.UploadToEngine()
			Assert-State "S2 change and upload" $s2 @{ Uploaded = $true; Refs = 1; Shaders = 1; Pack = 92; Created = 2; Destroyed = 1 } $start
			$packed = $s2.PackForEngine()
			$roughness = if ($packed.Length -ge 52) { [BitConverter]::ToSingle($packed, 48) } else { "(no data)" }
			Check ($roughness -eq [float]0.2) "S2 the packed data has the new roughness: $roughness"
			$s2.UnloadFromEngine()
			Assert-State "S2 unload" $s2 @{ Uploaded = $false; Refs = 0; Shaders = 0; Created = 2; Destroyed = 2 } $start

			# S3: 同じ内容の A と B。A を外して変えてアップロードし、B を外す
			$start = Get-MaterialCounts
			$a = New-Applied
			$b = New-Applied
			[void]$a.UploadToEngine()
			[void]$b.UploadToEngine()
			Assert-State "S3 A and B share" $b @{ Uploaded = $true; Refs = 2; Shaders = 2; Pack = 92; Created = 1; Destroyed = 0 } $start
			$a.UnloadFromEngine()
			Assert-State "S3 A unloaded (B)" $b @{ Uploaded = $true; Refs = 1; Shaders = 1; Pack = 92; Created = 1; Destroyed = 0 } $start
			$a.MaterialSurface.Roughness = 0.3
			[void]$a.UploadToEngine()
			Assert-State "S3 A changed and uploaded (A)" $a @{ Uploaded = $true; Refs = 1; Shaders = 2; Pack = 92; Created = 2; Destroyed = 0 } $start
			$b.UnloadFromEngine()
			Assert-State "S3 B unloaded (A)" $a @{ Uploaded = $true; Refs = 1; Shaders = 1; Pack = 92; Created = 2; Destroyed = 1 } $start
			$a.UnloadFromEngine()
			Assert-State "S3 A unloaded" $a @{ Uploaded = $false; Refs = 0; Shaders = 0; Created = 2; Destroyed = 2 } $start

			# S4: アップロードに失敗しても何も持たず、あとでアップロードできる
			$start = Get-MaterialCounts
			$s4 = New-Applied
			$script:fake.Fail = $true
			$failed = $s4.UploadToEngine()
			$script:fake.Fail = $false
			Assert-State "S4 failed upload (returns $failed)" $s4 @{ Uploaded = $false; Refs = 0; Shaders = 0; Created = 0; Destroyed = 0 } $start
			Check ($script:fakeShaders.Live.Count -eq 0) "S4 no shaders are left registered: $($script:fakeShaders.Live.Count)"
			[void]$s4.UploadToEngine()
			Assert-State "S4 upload after the failure" $s4 @{ Uploaded = $true; Refs = 1; Shaders = 1; Pack = 92; Created = 1; Destroyed = 0 } $start

			# S5: 内容を変えたアップロードに失敗しても、前のものを持ち続ける
			$s4.MaterialSurface.Roughness = 0.4
			$script:fake.Fail = $true
			$failed = $s4.UploadToEngine()
			$script:fake.Fail = $false
			Assert-State "S5 failed upload of changed content (returns $failed)" $s4 @{ Uploaded = $true; Refs = 1; Shaders = 1; Pack = 92; Created = 1; Destroyed = 0 } $start
			# PackForEngine() は今の値でパックする。アップロードしたデータ(GetMetadata().PackedData)は前のまま
			$packed = $s4.GetMetadata().PackedData
			$roughness = if ($packed.Length -ge 52) { [BitConverter]::ToSingle($packed, 48) } else { "(no data)" }
			Check ($packed.Length -eq 92 -and $roughness -ne [float]0.4 -and $getContentId.Invoke($null, @($s4.Guid)) -eq $s4.UploadedAsset.ContentId) "S5 it keeps the previous upload: uploaded roughness $roughness"
			$s4.UnloadFromEngine()
			Assert-State "S5 unload" $s4 @{ Uploaded = $false; Refs = 0; Shaders = 0; Created = 1; Destroyed = 1 } $start

			# S6: 入力のテクスチャは、同じ内容で2回アップロードしても1つだけ参照する
			$uploadedAssetType.GetProperty("LoadAsset", $flags).SetValue($null, [System.Management.Automation.LanguagePrimitives]::ConvertTo($loadSb, $seams.LoadAsset))
			$s6 = New-Applied
			$s6.Inputs[0].Asset = $texInfo
			[void]$s6.UploadToEngine()
			[void]$s6.UploadToEngine()
			$texRefs = if ($uploadedAssets.ContainsKey($texInfo.Guid)) { $uploadedAssets[$texInfo.Guid].ReferenceCount } else { 0 }
			Check ($texRefs -eq 1 -and $s6.PackForEngine().Length -eq 96) "S6 the input texture is referenced once: $texRefs, pack $($s6.PackForEngine().Length)"
			$s6.UnloadFromEngine()
			Check (-not $uploadedAssets.ContainsKey($texInfo.Guid) -and $uploadedAssets.Count -eq 0 -and $script:fakeShaders.Live.Count -eq 0) "S6 unloading releases the texture and the shaders: $($uploadedAssets.Count) assets, $($script:fakeShaders.Live.Count) shaders"

			# 読み込めないアセットはアップロードしない(本物の読み込みを使う)
			$uploadedAssetType.GetProperty("LoadAsset", $flags).SetValue($null, $originals.LoadAsset)
			function New-Info([string]$type, [string]$path) { $info = [Activator]::CreateInstance($assetInfoType); $info.Type = [Enum]::Parse($assetTypeEnum, $type); $info.Guid = [Guid]::NewGuid(); $info.FullPath = $path; $info }
			[string]$garbageFile = Join-Path $work "Garbage.asset"
			[System.IO.File]::WriteAllBytes($garbageFile, [byte[]](@(6, 0, 0, 0) + (1..20)))
			$createdBefore = $script:fake.Created.Count
			$cases = [ordered]@{
				"a texture that does not exist" = New-Info "Texture" (Join-Path $work "Missing.asset")
				"a broken texture" = New-Info "Texture" $garbageFile
				"a material file as a texture" = New-Info "Texture" $file
				"a mesh that does not exist" = New-Info "Mesh" (Join-Path $work "MissingMesh.asset")
			}
			foreach ($case in $cases.GetEnumerator())
			{
				$result = $addToScene.Invoke($null, @($case.Value, $null))
				Check ($null -eq $result -and $script:fake.Created.Count -eq $createdBefore -and $uploadedAssets.Count -eq 0) "$($case.Key) is not uploaded"
			}
		}
		finally
		{
			foreach ($name in $shaderOriginals.Keys) { $shaderGroupType.GetProperty($name, $flags).SetValue($null, $shaderOriginals[$name]) }
		}
	}
	finally
	{
		foreach ($name in $originals.Keys) { $uploadedAssetType.GetProperty($name, $flags).SetValue($null, $originals[$name]) }
	}
}
finally
{
	Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}

Write-Host ""
if ($skipped.Count -gt 0) { Write-Host "SKIPPED ($($skipped.Count)): DXForgeEngineDLL.dll がない(-RequireEngineDll で必須にできる)" }
if ($failures.Count -gt 0)
{
	Write-Host "FAILED ($($failures.Count))"
	exit 1
}
Write-Host "ALL PASSED"
exit 0
