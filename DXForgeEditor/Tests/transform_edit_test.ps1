<#
.SYNOPSIS
    インスペクターの Transform の編集(Primal Day 086、Issue #244)が、C# の値とエンジンのエンティティを同じに保つことを確かめる回帰テスト。

.DESCRIPTION
    ビルド済みの DXForgeEditor.dll と DXForgeEngineDLL.dll を読み込み、TransformOnly のプロジェクト(DXForgeCli/Tests/Projects)を
    エディタのコード(Project.Load)で開く。2 つのエンティティを DLL に作り(GameEntity.IsActive)、1 つはエンジンに作らずに、
    3 つを選んだ MSTransform(インスペクターのビューモデル)の値を変える。エンジンは初期化しない(GPU は要らない)。
    値はエンジンから読み戻さず、EngineAPI.EntityAPI.Get* はこのテストで比べるためだけに使う(docs/design/transform-editing.md)。

    確かめること:
      1. Position/Rotation/Scale の欄: 3 つの C# の値が変わり、エンジンにある 2 つはエンジンの値も同じになる。拡大率の一様のトグル。
      2. Local Offset: 回転したエンティティのローカル軸に沿って動く。
      3. Rotation Offset: Local と World で結果が違い、それぞれ C# の差分の掛け算(Concatenate)と一致する。手がかり(度数法のオイラー角)は
         クォータニオンと同じ回転を表す。DLL の Local/World(SetRotation の TransformSpace)とも同じ結果になる(C# の TransformSpace の値の確認)。
      4. Undo と同じ経路(Transform.ApplyPositions/ApplyRotations): 前の値に戻すとエンジンも戻る。エンジンにないエンティティは C# だけが変わる。
      5. 保存した .dxproj を読み直すと、クォータニオンと手がかりがそのまま残り(作り直されない)、同じ回転を表す。
         dxforge --frames(game.bin をランナーで読む)のエンジンの位置・回転・拡大率が C# の値と同じ。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。
.PARAMETER Runner
    5 の game.bin を読むランナー(既定は x64\Debug か x64\Release の EngineUnitTests.exe)。
.PARAMETER RequireEngineDll
    DXForgeEngineDLL.dll がなければ失敗にする(ないときは SKIP)。
.PARAMETER RequireRunner
    ランナーがなければ失敗にする(ないときは 5 の実行を SKIP)。

.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeEditor/Tests/transform_edit_test.ps1 -RequireEngineDll -RequireRunner
#>
[CmdletBinding()]
param(
	[ValidateSet("DebugEditor", "ReleaseEditor")]
	[string]$Configuration = "DebugEditor",
	[string]$Runner,
	[switch]$RequireEngineDll,
	[switch]$RequireRunner
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$OutDir = Join-Path $RepoRoot "x64\$Configuration"
$NativeConfiguration = if ($Configuration -eq "DebugEditor") { "Debug" } else { "Release" }
if (-not $Runner) { $Runner = Join-Path $RepoRoot "x64\$NativeConfiguration\EngineUnitTests.exe" }
$Cli = Join-Path $OutDir "dxforge.exe"
$failures = [System.Collections.Generic.List[string]]::new()
$skips = [System.Collections.Generic.List[string]]::new()

function Check([bool]$condition, [string]$message)
{
	Write-Host ($(if ($condition) { "  PASS " } else { "  FAIL " }) + $message)
	if (-not $condition) { $failures.Add($message) }
}

if ([System.Threading.Thread]::CurrentThread.GetApartmentState() -ne "STA") { throw "Run this script on an STA thread (pwsh -STA)." }

$engineDllPath = Join-Path $OutDir "DXForgeEngineDLL.dll"
if (-not (Test-Path $engineDllPath))
{
	if ($RequireEngineDll) { throw "$engineDllPath がありません(./build.ps1 -Target native -Configuration $Configuration -Project DXForgeEngineDLL)。" }
	Write-Host "SKIP: $engineDllPath がありません。"
	exit 0
}
Add-Type -AssemblyName PresentationFramework
# Logger(警告)が WPF の Application を使う
$null = [System.Windows.Application]::new()
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

$editor = [System.Reflection.Assembly]::LoadFrom((Join-Path $OutDir "DXForgeEditor.dll"))
$flags = [System.Reflection.BindingFlags]"Public,NonPublic,Static,Instance"
$editor.GetType("DXForgeEditor.GameProject.ProjectLoadEnvironment", $true).GetProperty("Headless", $flags).SetValue($null, $true)
$projectType = $editor.GetType("DXForgeEditor.GameProject.Project", $true)
$entityType = $editor.GetType("DXForgeEditor.Components.GameEntity", $true)
$transformType = $editor.GetType("DXForgeEditor.Components.Transform", $true)
$msTransformType = $editor.GetType("DXForgeEditor.Components.MSTransform", $true)
$msEntityType = $editor.GetType("DXForgeEditor.Components.MSGameEntity", $true)
$entityApi = $editor.GetType("DXForgeEditor.DllWrappers.EngineAPI+EntityAPI", $true)
$spaceType = $editor.GetType("DXForgeEditor.EngineAPIStructs.TransformSpace", $true)
$euler = $editor.GetType("DXForgeEditor.Utilities.EulerRotation", $true)
$serializer = $editor.GetType("DXForgeEditor.Utilities.Serializer", $true)
$load = $projectType.GetMethod("Load", $flags)

function Near([double]$a, [double]$b, [double]$tolerance = 1e-4) { return [Math]::Abs($a - $b) -le $tolerance }
function Near-Vector($a, $b, [double]$tolerance = 1e-4) { return (Near $a.X $b.X $tolerance) -and (Near $a.Y $b.Y $tolerance) -and (Near $a.Z $b.Z $tolerance) }
# q と -q は同じ回転
function Same-Rotation($a, $b) { return [Math]::Abs([System.Numerics.Quaternion]::Dot($a, $b)) -ge 1 - 1e-5 }
function Hint-Matches($t) { return [bool]$euler.GetMethod("IsSameRotation").Invoke($null, @($euler.GetMethod("FromDegrees").Invoke($null, @($t.RotationEulerHint)), $t.RotationQuaternion)) }
function Transform-Of($entity) { return @($entity.Components | Where-Object { $_.GetType() -eq $transformType })[0] }
function Deg([double]$x, [double]$y, [double]$z) { return $euler.GetMethod("FromDegrees").Invoke($null, @([System.Numerics.Vector3]::new($x, $y, $z))) }

# エンジンの値(比べるためだけに読む)
function Engine-Values($entity)
{
	$ids = [int[]]@($entity.EntityId)
	$position = [System.Numerics.Vector3[]]::new(1); $rotation = [System.Numerics.Quaternion[]]::new(1); $scale = [System.Numerics.Vector3[]]::new(1)
	$ok = $entityApi.GetMethod("GetPosition").Invoke($null, @($ids, $position)) -and $entityApi.GetMethod("GetRotation").Invoke($null, @($ids, $rotation)) -and $entityApi.GetMethod("GetScale").Invoke($null, @($ids, $scale))
	return [pscustomobject]@{ Ok = $ok; Position = $position[0]; Rotation = $rotation[0]; Scale = $scale[0] }
}
function Engine-Matches($entity)
{
	$t = Transform-Of $entity
	$e = Engine-Values $entity
	return $e.Ok -and (Near-Vector $e.Position $t.Position) -and (Same-Rotation $e.Rotation $t.RotationQuaternion) -and (Near-Vector $e.Scale $t.Scale)
}

# List<(Transform, T)> を作る(Undo のアクションと同じ引数)
function Tuple-List([type[]]$types, [object[][]]$rows)
{
	$tupleType = $(if ($types.Count -eq 2) { [System.ValueTuple[int, int]] } else { [System.ValueTuple[int, int, int]] }).GetGenericTypeDefinition().MakeGenericType($types)
	$list = [Activator]::CreateInstance([System.Collections.Generic.List[int]].GetGenericTypeDefinition().MakeGenericType($tupleType))
	foreach ($row in $rows) { $list.Add([Activator]::CreateInstance($tupleType, [object[]]@($row | ForEach-Object { $_.PSObject.BaseObject }))) }
	# NOTE: そのまま返すと PowerShell が要素に分ける
	return ,$list
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) "dxforge_transform_edit_test_$([Guid]::NewGuid().ToString('N'))"
$null = New-Item -ItemType Directory -Force (Join-Path $work "TransformOnly")
$projectFile = Join-Path $work "TransformOnly\TransformOnly.dxproj"
Copy-Item (Join-Path $RepoRoot "DXForgeCli\Tests\Projects\TransformOnly\TransformOnly.dxproj") $projectFile
$active = @()
try
{
	$project = $load.Invoke($null, [object[]]@([string]$projectFile)).PSObject.BaseObject
	$entities = @($project.ActiveScene.GameEntities)
	Check ($entities.Count -ge 3) "the fixture's active scene has 3 entities ($($entities.Count))"
	$inEngine = @($entities[0], $entities[1])
	$notInEngine = $entities[2]
	foreach ($e in $inEngine) { $e.IsActive = $true; $active += $e }
	Check ((@($inEngine | Where-Object { $_.EntityId -ge 0 }).Count -eq 2) -and $notInEngine.EntityId -eq -1) "two entities are created in the DLL, the third is not ($($inEngine[0].EntityId), $($inEngine[1].EntityId), $($notInEngine.EntityId))"

	$selection = [System.Collections.Generic.List[object]]::new()
	$typedSelection = [Activator]::CreateInstance([System.Collections.Generic.List[int]].GetGenericTypeDefinition().MakeGenericType($entityType))
	foreach ($e in $entities[0..2]) { $typedSelection.Add($e) }
	$msEntity = [Activator]::CreateInstance($msEntityType, $flags, $null, [object[]]@(, $typedSelection), $null)
	$ms = $msEntity.GetType().GetMethod("GetMSComponent").MakeGenericMethod($msTransformType).Invoke($msEntity, @())
	$transforms = @($entities[0..2] | ForEach-Object { Transform-Of $_ })

	Write-Host "1. Position, Rotation, Scale"
	$ms.PosX = 2
	Check ((@($transforms | Where-Object { Near $_.Position.X 2 }).Count -eq 3) -and (Engine-Matches $inEngine[0]) -and (Engine-Matches $inEngine[1])) "PosX 2: all three in C#, the two in the engine match"
	$ms.RotX = 0; $ms.RotY = 90; $ms.RotZ = 0
	Check ((@($transforms | Where-Object { Same-Rotation $_.RotationQuaternion (Deg 0 90 0) }).Count -eq 3) -and (Engine-Matches $inEngine[0]) -and (Engine-Matches $inEngine[1])) "Rotation (0, 90, 0): the quaternion follows in C# and in the engine"
	$ms.IsUniformScale = $true
	$ms.ScaleX = 2
	Check ((@($transforms | Where-Object { Near-Vector $_.Scale ([System.Numerics.Vector3]::new(2, 2, 2)) }).Count -eq 3) -and $ms.ScaleY -eq 2 -and (Engine-Matches $inEngine[0])) "uniform scale on: ScaleX 2 -> (2, 2, 2), the box shows it"
	$ms.IsUniformScale = $false
	$ms.ScaleY = 3
	Check ((@($transforms | Where-Object { Near-Vector $_.Scale ([System.Numerics.Vector3]::new(2, 3, 2)) }).Count -eq 3) -and (Engine-Matches $inEngine[1])) "uniform scale off: ScaleY 3 -> (2, 3, 2)"

	Write-Host "2. Local Offset"
	$before = @($transforms | ForEach-Object { $_.Position })
	$ms.LocalPosZ = 1
	# ヨー 90 度のエンティティの front(ローカルの +z)はワールドの +x
	$moved = @(0..2 | Where-Object { Near-Vector $transforms[$_].Position ($before[$_] + [System.Numerics.Vector3]::new(1, 0, 0)) }).Count
	Check ($moved -eq 3 -and (Engine-Matches $inEngine[0]) -and (Engine-Matches $inEngine[1]) -and (Near $ms.PosX 3)) "LocalPosZ 1 with yaw 90: each moves +1 along world X (its front), the Position box shows 3 ($moved, $($ms.PosX))"
	$ms.LocalPosZ = 1.5
	Check ((Near-Vector $transforms[0].Position ($before[0] + [System.Numerics.Vector3]::new(1.5, 0, 0))) -and (Engine-Matches $inEngine[0])) "dragging on to 1.5 adds only the difference (+0.5)"
	$ms.ResetOffsets()
	Check ($ms.LocalPosZ -eq 0 -and (Near-Vector $transforms[0].Position ($before[0] + [System.Numerics.Vector3]::new(1.5, 0, 0)))) "ResetOffsets (on commit) shows 0 and does not move the entities"

	Write-Host "3. Rotation Offset: local and world"
	$q0 = $transforms[0].RotationQuaternion
	$dx = Deg 90 0 0
	$ms.IsLocalRotation = $true
	$ms.RotOffsetX = 90
	$local = $transforms[0].RotationQuaternion
	Check ((Same-Rotation $local ([System.Numerics.Quaternion]::Concatenate($dx, $q0))) -and (Hint-Matches $transforms[0]) -and (Engine-Matches $inEngine[0]) -and (Engine-Matches $inEngine[1])) "local: q' = Concatenate(delta, q) (delta about the entity's axes), the hint matches, the engine matches"
	Check ((Same-Rotation $transforms[2].RotationQuaternion $local) -and (Hint-Matches $transforms[2])) "the entity that is not in the engine gets the same rotation in C# only"
	# 選択を元の回転に戻す(Undo と同じ経路)
	$rotationTypes = [type[]]@($transformType, [System.Numerics.Quaternion], [System.Numerics.Vector3])
	$hint0 = $euler.GetMethod("ToDegrees").Invoke($null, @($q0))
	$transformType.GetMethod("ApplyRotations").Invoke($null, @(, (Tuple-List $rotationTypes @(@($transforms[0], $q0, $hint0), @($transforms[1], $q0, $hint0), @($transforms[2], $q0, $hint0)))))
	Check ((Same-Rotation $transforms[0].RotationQuaternion $q0) -and (Engine-Matches $inEngine[0]) -and (Engine-Matches $inEngine[1])) "ApplyRotations (the undo path) puts C# and the engine back"
	$ms.ResetOffsets()
	$ms.IsLocalRotation = $false
	$ms.RotOffsetX = 90
	$world = $transforms[0].RotationQuaternion
	Check ((Same-Rotation $world ([System.Numerics.Quaternion]::Concatenate($q0, $dx))) -and (Hint-Matches $transforms[0]) -and (Engine-Matches $inEngine[0])) "world: q' = Concatenate(q, delta) (delta about the world axes), the hint matches, the engine matches"
	Check (-not (Same-Rotation $local $world)) "local and world give different rotations for the rotated entity"

	# C# の差分の計算と、DLL の Local/World(PR 1)が同じ結果になる。C# の TransformSpace の値が C++ と同じことの確認でもある
	foreach ($case in @(@{ Name = "Local"; Expected = $local }, @{ Name = "World"; Expected = $world }))
	{
		$transformType.GetMethod("ApplyRotations").Invoke($null, @(, (Tuple-List $rotationTypes @(, @($transforms[0], $q0, $hint0)))))
		$ok = $entityApi.GetMethod("SetRotation").Invoke($null, @([int[]]@($inEngine[0].EntityId), [System.Numerics.Quaternion[]]@($dx), [Enum]::Parse($spaceType, $case.Name)))
		$e = Engine-Values $inEngine[0]
		Check ($ok -and (Same-Rotation $e.Rotation $case.Expected)) "the DLL's TransformSpace.$($case.Name) gives the same rotation as the editor's $($case.Name.ToLower()) offset"
	}
	# 試したエンジンの回転を、C# の値に戻す
	$transformType.GetMethod("ApplyRotations").Invoke($null, @(, (Tuple-List $rotationTypes @(, @($transforms[0], $world, $euler.GetMethod("ToDegrees").Invoke($null, @($world)))))))
	Check (Engine-Matches $inEngine[0]) "the engine is back in step with C#"

	Write-Host "4. the undo path for positions, entities that are not in the engine"
	$positionTypes = [type[]]@($transformType, [System.Numerics.Vector3])
	$transformType.GetMethod("ApplyPositions").Invoke($null, @(, (Tuple-List $positionTypes @(@($transforms[0], $before[0]), @($transforms[1], $before[1]), @($transforms[2], $before[2])))))
	Check ((Near-Vector $transforms[0].Position $before[0]) -and (Near-Vector $transforms[2].Position $before[2]) -and (Engine-Matches $inEngine[0]) -and (Engine-Matches $inEngine[1])) "ApplyPositions with the old values (undo) moves C# and the engine back"
	$inEngine[1].IsActive = $false
	$active = @($inEngine[0])
	$ms.PosY = 5
	Check ((Near $transforms[1].Position.Y 5) -and $inEngine[1].EntityId -eq -1 -and (Engine-Matches $inEngine[0])) "after removing an entity from the engine, editing still works and changes it in C# only"

	Write-Host "5. save, reload, game.bin"
	$expected = @($transforms | ForEach-Object { [pscustomobject]@{ Position = $_.Position; Rotation = $_.RotationQuaternion; Hint = $_.RotationEulerHint; Scale = $_.Scale } })
	$serializer.GetMethod("ToFile").MakeGenericMethod($projectType).Invoke($null, [object[]]@($project, [string]$projectFile))
	$reloaded = @($load.Invoke($null, [object[]]@([string]$projectFile)).PSObject.BaseObject.ActiveScene.GameEntities)
	$kept = @(0..2 | Where-Object {
		$t = Transform-Of $reloaded[$_]
		(Same-Rotation $t.RotationQuaternion $expected[$_].Rotation) -and (Near-Vector $t.RotationEulerHint $expected[$_].Hint 1e-5) -and (Hint-Matches $t) -and (Near-Vector $t.Position $expected[$_].Position)
	}).Count
	Check ($kept -eq 3) "the reloaded .dxproj keeps the quaternion and the hint as saved, and they agree ($kept of 3)"
	if (-not (Test-Path $Runner))
	{
		if ($RequireRunner) { Check $false "runner exists: $Runner" }
		else { Write-Host "  SKIP game.bin (no runner at $Runner)"; $skips.Add("game.bin") }
	}
	else
	{
		$output = & $Cli --load-project $projectFile --frames 1 --runner $Runner --json 2>&1
		$json = ($output | Where-Object { $_ -is [string] }) -join "`n" | ConvertFrom-Json
		$run = @($json.entities)
		$same = 0
		foreach ($i in 0..2)
		{
			$name = $reloaded[$i].Name
			$r = @($run | Where-Object { $_.name -eq $name })[0]
			if ($r -and (Near-Vector ([System.Numerics.Vector3]::new($r.position[0], $r.position[1], $r.position[2])) $expected[$i].Position) -and
				(Same-Rotation ([System.Numerics.Quaternion]::new($r.rotation[0], $r.rotation[1], $r.rotation[2], $r.rotation[3])) $expected[$i].Rotation) -and
				(Near-Vector ([System.Numerics.Vector3]::new($r.scale[0], $r.scale[1], $r.scale[2])) $expected[$i].Scale)) { ++$same }
		}
		Check ($LASTEXITCODE -eq 0 -and $same -eq 3) "dxforge --frames: the engine reads game.bin with the same position, quaternion and scale as C# ($same of 3, exit $LASTEXITCODE)"
	}
}
finally
{
	foreach ($e in $active) { try { $e.IsActive = $false } catch { Write-Host "  cleanup: $_" } }
	Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}

Write-Host ""
if ($skips.Count) { Write-Host "Skipped: $($skips -join ', ')" }
if ($failures.Count -gt 0)
{
	Write-Host "FAILED ($($failures.Count))"
	exit 1
}
Write-Host "ALL PASSED"
exit 0
