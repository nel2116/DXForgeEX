<#
.SYNOPSIS
    エディタの既定のジオメトリ(DefaultCube.asset, Primal Day 077/080, Issue #32)を確かめる回帰テスト。

.DESCRIPTION
    ビルド済みの DXForgeEditor.dll を読み込み、DefaultAssets.CreateDefaultCube()(GenerateDefaultAssets() が
    Resources\DefaultAssets\DefaultCube.asset がないときに呼ぶもの)をリフレクションで呼んで、%TEMP% の下に作る。
    エディタのウィンドウは開かず、GPU もエンジンも使わない。立方体は x64\<Configuration>\ の ContentTools.dll
    (CreatePrimitiveMesh)で作る。ContentTools.dll は FBX SDK が要り CI ではビルドできないので、ローカルで実行する。
    Geometry.Save() のアイコンに WPF が要るので、WPF の Application を作ってから呼ぶ。

    確かめること:
      1. ジオメトリのアセットとして保存され、アイコンがある。
      2. 読み戻したメタデータが、LOD グループ "cube" の LOD 1 つ・メッシュ 1 つ・24 頂点・36 インデックス・12 三角形
         (ContentTools/Tests/primitive_mesh_cube_test.py が確かめる create_cube の既定の分割数 1x1x1 と同じ)。
      3. エンジンにアップロードする形(Geometry.PackForEngine())が作れる。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。

.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeEditor/Tests/default_geometry_test.ps1
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

$toolsDllPath = Join-Path $OutDir "ContentTools.dll"
if (-not (Test-Path $toolsDllPath)) { throw "$toolsDllPath がありません(./build.ps1 -Target native -Configuration $Configuration -Project ContentTools)。" }

Add-Type -AssemblyName PresentationFramework
$null = [System.Windows.Application]::new()
[void][System.Runtime.InteropServices.NativeLibrary]::Load($toolsDllPath)
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
# Geometry.Save() のアイコン(GeometryView)はエディタの色(App.xaml が読み込む辞書)を使うので、同じ辞書を読み込む
foreach ($name in @("EditorColors", "ControlTemplates"))
{
	$dictionary = [System.Windows.Application]::LoadComponent([Uri]::new("/DXForgeEditor;component/Dictionaries/$name.xaml", [UriKind]::Relative))
	[System.Windows.Application]::Current.Resources.MergedDictionaries.Add($dictionary)
}
$defaultAssets = $editor.GetType("DXForgeEditor.Content.DefaultAssets", $true)
$assetType = $editor.GetType("DXForgeEditor.Content.Asset", $true)
$geometryType = $editor.GetType("DXForgeEditor.Content.Geometry", $true)

$work = Join-Path ([System.IO.Path]::GetTempPath()) "default_geometry_test_$([Guid]::NewGuid().ToString('N'))"
$null = New-Item -ItemType Directory -Force $work
try
{
	Write-Host "1. DefaultAssets.CreateDefaultCube"
	[string]$file = Join-Path $work "DefaultCube.asset"
	$null = $defaultAssets.GetMethod("CreateDefaultCube", $flags).Invoke($null, @($file))
	$info = $assetType.GetMethod("TryGetAssetInfo", $flags).Invoke($null, @($file))
	Check ((Test-Path $file) -and $info -and "$($info.Type)" -eq "Mesh" -and $info.Guid -ne [Guid]::Empty -and $info.Icon.Length -gt 0) "saves a geometry asset with an icon: $($info.Type), icon $($info.Icon.Length) bytes"

	Write-Host "2. metadata"
	$geometry = $geometryType.GetMethod("FromAssetInfo", $flags).Invoke($null, @($info))
	$metadata = if ($geometry) { $geometry.GetMetadata() } else { $null }
	$lods = @($metadata.LODs)
	$meshes = if ($lods.Count -eq 1) { @($lods[0].Meshes) } else { @() }
	Check ($lods.Count -eq 1 -and $meshes.Count -eq 1) "one LOD with one mesh (LODs $($lods.Count), meshes $($meshes.Count))"
	Check ($meshes.Count -eq 1 -and $meshes[0].VertexCount -eq 24 -and $meshes[0].IndexCount -eq 36 -and $meshes[0].TriangleCount -eq 12) "24 vertices, 36 indices, 12 triangles (V $($meshes[0].VertexCount), I $($meshes[0].IndexCount), T $($meshes[0].TriangleCount))"
	Check ($metadata.Name -eq "cube") "the LOD group is named cube ($($metadata.Name))"

	Write-Host "3. PackForEngine"
	$packed = if ($geometry) { $geometry.PackForEngine() } else { $null }
	Check ($null -ne $packed -and $packed.Length -gt 0) "packs the geometry for the engine ($($packed.Length) bytes)"
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
