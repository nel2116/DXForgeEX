<#
.SYNOPSIS
    Primitive mesh のダイアログの Cube の設定欄(Primal Day 075, Issue #179)を確かめる回帰テスト。

.DESCRIPTION
    ビルド済みの DXForgeEditor.dll を読み込み、PrimitiveMeshDialog を表示せずに作って、設定欄の値を変える。
    ウィンドウは開かず(描画と Save のダイアログは確かめない)、GPU もエンジンも使わない。メッシュは x64\<Configuration>\ の
    ContentTools.dll(CreatePrimitiveMesh)で作る。ContentTools.dll は FBX SDK が要り CI ではビルドできないので、ローカルで実行する。

    確かめること:
      1. Cube を選ぶと Cube の設定欄だけが表示され、LOD グループ "cube" の 24 頂点・36 インデックスのメッシュができる。
      2. 分割数のスライダーで、頂点数とインデックス数が変わる(面ごとに (a+1)(b+1) 頂点、6ab インデックス)。
      3. 大きさの欄で、頂点の位置の範囲が変わる(-size〜size)。LOD の欄を変えても壊れず、数字でない値でも止まらない。
      4. 作ったジオメトリをアセットとして保存でき、読み戻したメタデータとエンジン用の形が合っている。
      5. Plane と UV Sphere の設定欄の動作が変わらない。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。

.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeEditor/Tests/primitive_mesh_dialog_test.ps1
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
# ダイアログのテクスチャ(pack://application:,,,/Resources/...)を、pwsh ではなくエディタの DLL から探させる。
# Application.ResourceAssembly は読み込みの時点で pwsh に決まっていて設定できないので、その値の置き場所を直接書き換える
$editor = [System.Reflection.Assembly]::LoadFrom((Join-Path $OutDir "DXForgeEditor.dll"))
$privateStatic = [System.Reflection.BindingFlags]"NonPublic,Static"
[System.Windows.Application].GetField("_resourceAssembly", $privateStatic).SetValue($null, $editor)
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
$flags = [System.Reflection.BindingFlags]"Public,NonPublic,Static,Instance"
# ダイアログのスタイルと色(App.xaml が読み込む辞書)を読み込む。PrimitiveMeshDialog の読み込みに要る
foreach ($name in @("EditorColors", "ControlTemplates"))
{
	$dictionary = [System.Windows.Application]::LoadComponent([Uri]::new("/DXForgeEditor;component/Dictionaries/$name.xaml", [UriKind]::Relative))
	[System.Windows.Application]::Current.Resources.MergedDictionaries.Add($dictionary)
}
$assetType = $editor.GetType("DXForgeEditor.Content.Asset", $true)
$geometryType = $editor.GetType("DXForgeEditor.Content.Geometry", $true)
$dialogType = $editor.GetType("DXForgeEditor.Content.PrimitiveMeshDialog", $true)

$dialog = [Activator]::CreateInstance($dialogType)
# Loaded イベントと同じ最初の更新。表示しないので、ここで呼ぶ
$updatePrimitive = $dialogType.GetMethod("UpdatePrimitive", $flags)
$null = $updatePrimitive.Invoke($dialog, $null)

function Control([string]$name) { return $dialogType.GetField($name, $flags).GetValue($dialog) }

function Pump
{
	[System.Windows.Threading.Dispatcher]::CurrentDispatcher.Invoke([Action]{}, [System.Windows.Threading.DispatcherPriority]::ApplicationIdle)
}

function Select-Primitive([string]$type)
{
	(Control "primTypeComboBox").SelectedItem = [Enum]::Parse($editor.GetType("DXForgeEditor.Content.PrimitiveMeshType", $true), $type)
	Pump
}

# 設定欄(グリッドの直下の StackPanel)を、中のコントロールから探す
function Settings-Panel([string]$controlName)
{
	$current = Control $controlName
	while ($true)
	{
		$parent = [System.Windows.LogicalTreeHelper]::GetParent($current)
		if ($parent -is [System.Windows.Controls.Grid] -and $parent.ColumnDefinitions.Count -eq 2) { return $current }
		$current = $parent
	}
}

function Current-Mesh
{
	$meshes = @($dialog.DataContext.MeshRenderer.Meshes)
	$positions = @($meshes[0].Positions)
	$xs = @($positions | ForEach-Object { $_.X } | Measure-Object -Minimum -Maximum)
	$ys = @($positions | ForEach-Object { $_.Y } | Measure-Object -Minimum -Maximum)
	$zs = @($positions | ForEach-Object { $_.Z } | Measure-Object -Minimum -Maximum)
	return [pscustomobject]@{
		Group    = $dialog.DataContext.Geometry.GetLODGroup().Name
		Meshes   = $meshes.Count
		Vertices = $positions.Count
		Indices  = @($meshes[0].Indices).Count
		SizeX    = $xs[0].Maximum
		SizeY    = $ys[0].Maximum
		SizeZ    = $zs[0].Maximum
		MinX     = $xs[0].Minimum
	}
}

function Near([double]$a, [double]$b) { return [Math]::Abs($a - $b) -lt 1e-3 }

$work = Join-Path ([System.IO.Path]::GetTempPath()) "primitive_mesh_dialog_test_$([Guid]::NewGuid().ToString('N'))"
$null = New-Item -ItemType Directory -Force $work
try
{
	Write-Host "1. selecting Cube"
	Select-Primitive "Cube"
	$cubePanel = Settings-Panel "xSliderCube"
	$planePanel = Settings-Panel "xSliderPlane"
	$spherePanel = Settings-Panel "xSliderUvSphere"
	Check ($cubePanel.Visibility -eq "Visible" -and $planePanel.Visibility -eq "Collapsed" -and $spherePanel.Visibility -eq "Collapsed") "only the cube settings are visible (cube $($cubePanel.Visibility), plane $($planePanel.Visibility), sphere $($spherePanel.Visibility))"
	$mesh = Current-Mesh
	Check ($mesh.Group -eq "cube" -and $mesh.Meshes -eq 1) "the LOD group cube has one mesh ($($mesh.Group), $($mesh.Meshes))"
	Check ($mesh.Vertices -eq 24 -and $mesh.Indices -eq 36) "1x1x1 -> 24 vertices, 36 indices (V $($mesh.Vertices), I $($mesh.Indices))"
	Check ((Near $mesh.SizeX 1) -and (Near $mesh.MinX -1)) "size 1 -> X from -1 to 1 (same as Primal; $($mesh.MinX) to $($mesh.SizeX))"

	Write-Host "2. segments"
	(Control "xSliderCube").Value = 2
	(Control "ySliderCube").Value = 3
	(Control "zSliderCube").Value = 4
	$mesh = Current-Mesh
	# X の面は z と y、Y の面は x と z、Z の面は x と y で分割する
	$expectedVertices = 2 * (5 * 4 + 3 * 5 + 3 * 4)
	$expectedIndices = 2 * 6 * (4 * 3 + 2 * 4 + 2 * 3)
	Check ($mesh.Vertices -eq $expectedVertices -and $mesh.Indices -eq $expectedIndices) "2x3x4 -> $expectedVertices vertices, $expectedIndices indices (V $($mesh.Vertices), I $($mesh.Indices))"

	Write-Host "3. size and LOD"
	(Control "xScalarBoxCube").Value = "2"
	(Control "yScalarBoxCube").Value = "0.5"
	(Control "zScalarBoxCube").Value = "3"
	$mesh = Current-Mesh
	Check ((Near $mesh.SizeX 2) -and (Near $mesh.MinX -2) -and (Near $mesh.SizeY 0.5) -and (Near $mesh.SizeZ 3)) "size 2, 0.5, 3 -> the extents follow ($($mesh.SizeX), $($mesh.SizeY), $($mesh.SizeZ))"
	(Control "xScalarBoxCube").Value = "not a number"
	$mesh = Current-Mesh
	Check (Near $mesh.SizeX 0.001) "a size that is not a number becomes the minimum 0.001 ($($mesh.SizeX))"
	(Control "xScalarBoxCube").Value = "2"
	$before = Current-Mesh
	foreach ($lod in @("1", "-1", "abc", ""))
	{
		(Control "lodTextBoxCube").Text = $lod
	}
	$mesh = Current-Mesh
	Check ($mesh.Vertices -eq $before.Vertices -and $mesh.Indices -eq $before.Indices) "the LOD box accepts any text and the mesh stays the same (create_cube does not use it, same as Primal)"
	(Control "lodTextBoxCube").Text = "0"

	Write-Host "4. saving the asset"
	[string]$file = Join-Path $work "cube.asset"
	$null = $dialog.DataContext.Geometry.Save($file)
	$info = $assetType.GetMethod("TryGetAssetInfo", $flags).Invoke($null, @($file))
	Check ((Test-Path $file) -and $info -and "$($info.Type)" -eq "Mesh" -and $info.Icon.Length -gt 0) "saves a geometry asset with an icon: $($info.Type), icon $($info.Icon.Length) bytes"
	$geometry = $geometryType.GetMethod("FromAssetInfo", $flags).Invoke($null, @($info))
	$metadata = if ($geometry) { $geometry.GetMetadata() } else { $null }
	$savedMeshes = @(@($metadata.LODs)[0].Meshes)
	Check ($metadata.Name -eq "cube" -and $savedMeshes.Count -eq 1 -and $savedMeshes[0].VertexCount -eq $expectedVertices -and $savedMeshes[0].IndexCount -eq $expectedIndices) "the saved metadata matches ($($metadata.Name), V $($savedMeshes[0].VertexCount), I $($savedMeshes[0].IndexCount))"
	$packed = if ($geometry) { $geometry.PackForEngine() } else { $null }
	Check ($null -ne $packed -and $packed.Length -gt 0) "packs the geometry for the engine ($($packed.Length) bytes)"

	Write-Host "5. the other primitives"
	Select-Primitive "Plane"
	$mesh = Current-Mesh
	Check ($planePanel.Visibility -eq "Visible" -and $cubePanel.Visibility -eq "Collapsed") "Plane shows its own settings (plane $($planePanel.Visibility), cube $($cubePanel.Visibility))"
	Check ($mesh.Group -eq "plane" -and $mesh.Vertices -eq 4 -and $mesh.Indices -eq 6) "Plane 1x1 -> 4 vertices, 6 indices ($($mesh.Group), V $($mesh.Vertices), I $($mesh.Indices))"
	(Control "xSliderPlane").Value = 2
	$mesh = Current-Mesh
	Check ($mesh.Vertices -eq 6 -and $mesh.Indices -eq 12) "Plane 2x1 -> 6 vertices, 12 indices (V $($mesh.Vertices), I $($mesh.Indices))"

	Select-Primitive "UvSphere"
	$mesh = Current-Mesh
	Check ($spherePanel.Visibility -eq "Visible" -and $cubePanel.Visibility -eq "Collapsed") "UV Sphere shows its own settings (sphere $($spherePanel.Visibility), cube $($cubePanel.Visibility))"
	Check ($mesh.Group -eq "uv_sphere" -and $mesh.Vertices -gt 0) "UV Sphere makes a uv_sphere mesh ($($mesh.Group), V $($mesh.Vertices), I $($mesh.Indices))"
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
