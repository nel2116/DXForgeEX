<#
.SYNOPSIS
    dxforge の操作(--ops, --op, --dry-run、Issue #183)の回帰テスト。

.DESCRIPTION
    x64\<Configuration>\dxforge.exe を別のプロセスとして実行し、終了コード、標準出力のJSON、.dxproj のバイト列を確かめる。
    エディタのウィンドウもGPUも使わない。エディタのコード(Project.Load とロックファイル)はリフレクションで呼ぶ。

    確かめること:
      1. 引数の誤り(2)、--help、ops.schema.json と SceneOpDocument の操作とキーの一致。
      2. ops の形式の誤り(40): どこが違うか(op_index と key)を返し、何も実行せず、.dxproj は変わらない。
      3. すべての操作の成功(読むだけなら保存しない、値が変わらなければ保存しない)。セレクターとアセットの指定のすべて。
      4. 対象が見つからない(41)、操作の失敗(42)。
      5. --dry-run と途中の失敗で .dxproj が 1 バイトも変わらない。保存の失敗(43)。一時ファイルが残らない。
      6. エディタが開いている(44): エディタと同じ ProjectEditorLock をテストのプロセスで持つ。読むだけなら通り、離す・切り替えると通る。2 つのエディタが持つなら、両方が離すまで断る。
      7. 空のプロジェクトに作って保存し、エディタのコード(Project.Load)で読むと同じ内容で、--frames で実行できる。
      8. GUID のない .dxproj。保存し直しても game.bin が変わらない。日本語の名前。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。

.PARAMETER Runner
    ヘッドレスのランナー。既定は dxforge と同じく x64\<Debug|Release>\EngineUnitTests.exe。

.PARAMETER RequireRunner
    ランナーがないときに、実行のケースを SKIP にせず失敗にする(CI はこれを付ける)。

.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeCli/Tests/ops_cli_test.ps1 -RequireRunner
#>
[CmdletBinding()]
param(
	[ValidateSet("DebugEditor", "ReleaseEditor")]
	[string]$Configuration = "DebugEditor",
	[string]$Runner,
	[switch]$RequireRunner
)

$ErrorActionPreference = "Stop"
if ([System.Threading.Thread]::CurrentThread.GetApartmentState() -ne "STA") { throw "Run this script on an STA thread (pwsh -STA)." }
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$OutDir = Join-Path $RepoRoot "x64\$Configuration"
$Cli = Join-Path $OutDir "dxforge.exe"
$NativeConfiguration = if ($Configuration -eq "DebugEditor") { "Debug" } else { "Release" }
if (-not $Runner) { $Runner = Join-Path $RepoRoot "x64\$NativeConfiguration\EngineUnitTests.exe" }
$Fixtures = Join-Path $PSScriptRoot "Projects"
$TransformOnly = Join-Path $Fixtures "TransformOnly\TransformOnly.dxproj"
$Template = Join-Path $RepoRoot "DXForgeEditor\ProjectTemplates\EmptyProject\project.dxproj"
$SchemaFile = Join-Path $PSScriptRoot "..\Schema\ops.schema.json"
$DefaultCubeGuid = "6c0f1b3e-8d2a-4f57-9e41-d5a7c3b20f19"
# TransformOnly の固定の GUID
$OriginGuid = "00000001-0000-4000-8000-000000000001"
$MovedGuid = "00000001-0000-4000-8000-000000000002"
$failures = [System.Collections.Generic.List[string]]::new()
$skips = [System.Collections.Generic.List[string]]::new()

function Check([bool]$condition, [string]$message, $detail = $null)
{
	Write-Host ($(if ($condition) { "  PASS " } else { "  FAIL " }) + $message)
	if (-not $condition)
	{
		if ($null -ne $detail) { Write-Host "       $detail" }
		$failures.Add($message)
	}
}

function Invoke-Cli([string[]]$Arguments, [switch]$Json)
{
	$psi = [System.Diagnostics.ProcessStartInfo]::new($Cli)
	$psi.UseShellExecute = $false
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	$psi.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
	$psi.StandardErrorEncoding = [System.Text.UTF8Encoding]::new($false)
	foreach ($a in $Arguments) { $psi.ArgumentList.Add($a) }
	if ($Json) { $psi.ArgumentList.Add("--json") }
	$p = [System.Diagnostics.Process]::Start($psi)
	$stderrTask = $p.StandardError.ReadToEndAsync()
	$stdout = $p.StandardOutput.ReadToEnd()
	if (-not $p.WaitForExit(120000))
	{
		$p.Kill($true)
		throw "dxforge did not exit within 120 s: $($Arguments -join ' ')"
	}
	$result = [pscustomobject]@{ Code = $p.ExitCode; Stdout = $stdout; Stderr = $stderrTask.Result; Json = $null }
	$p.Dispose()
	if ($Json)
	{
		$lines = @($stdout -split "`r?`n" | Where-Object { $_ -ne "" })
		try { $result.Json = $stdout | ConvertFrom-Json } catch { $result.Json = $null }
		if ($lines.Count -ne 1 -or $null -eq $result.Json) { Check $false "stdout is exactly one JSON object ($($Arguments -join ' '))" $stdout }
	}
	return $result
}

function Near([double[]]$actual, [double[]]$expected, [double]$tolerance = 1e-4)
{
	if ($actual.Count -ne $expected.Count) { return $false }
	for ($i = 0; $i -lt $actual.Count; ++$i) { if ([Math]::Abs($actual[$i] - $expected[$i]) -gt $tolerance) { return $false } }
	return $true
}

function Bytes-Equal([byte[]]$a, [byte[]]$b)
{
	return $null -ne $a -and $null -ne $b -and [System.Linq.Enumerable]::SequenceEqual($a, $b)
}

function Read-Bytes([string]$path) { return ,[System.IO.File]::ReadAllBytes($path) }

$script:projectCounter = 0
# TransformOnly のコピーを新しいフォルダに作る(.dxproj の中の Path は別の場所を指したまま)
function New-FixtureCopy
{
	$folder = Join-Path $work ("copy" + (++$script:projectCounter))
	$null = New-Item -ItemType Directory -Force $folder
	$file = Join-Path $folder "TransformOnly.dxproj"
	[System.IO.File]::Copy($TransformOnly, $file)
	return $file
}

# テンプレートと同じ空のプロジェクト(エンティティなし、Path は実際のフォルダ)
function New-EmptyProject([string]$name)
{
	$folder = Join-Path $work "$name\"
	$null = New-Item -ItemType Directory -Force $folder
	$file = Join-Path $folder "$name.dxproj"
	$xml = [System.IO.File]::ReadAllText($Template).Replace("{0}", $name).Replace("{1}", $folder)
	[System.IO.File]::WriteAllText($file, $xml)
	return $file
}

function Write-OpsFile([string[]]$ops)
{
	$file = Join-Path $work ("ops" + (++$script:projectCounter) + ".json")
	[System.IO.File]::WriteAllText($file, '{"ops":[' + ($ops -join ",") + ']}')
	return $file
}

# ops を --ops で実行する
function Invoke-Ops([string]$project, [string[]]$ops, [switch]$DryRun)
{
	$arguments = @("--load-project", $project, "--ops", (Write-OpsFile $ops))
	if ($DryRun) { $arguments += "--dry-run" }
	return Invoke-Cli $arguments -Json
}

function Invoke-Op([string]$project, [string]$op, [switch]$DryRun)
{
	$arguments = @("--load-project", $project, "--op", $op)
	if ($DryRun) { $arguments += "--dry-run" }
	return Invoke-Cli $arguments -Json
}

function Expect-Failure([string]$name, $r, [int]$code, [string]$errorCode)
{
	Check ($r.Code -eq $code -and $r.Json.exit_code -eq $code -and $r.Json.success -eq $false -and $r.Json.error.code -eq $errorCode -and $r.Json.changed -eq $false -and $r.Json.saved -eq $false) "$name -> exit $code, error.code $errorCode, changed/saved false" "$($r.Code) $($r.Stdout) $($r.Stderr)"
	Check (-not ($r.Stderr -match "Unhandled exception|FailFast")) "$name -> no crash" $r.Stderr
}

# 実行しても .dxproj が 1 バイトも変わらないことも確かめる
function Expect-Unchanged([string]$name, [string]$project, [byte[]]$before, [int]$code, [string]$errorCode, [scriptblock]$run)
{
	$r = & $run
	Expect-Failure $name $r $code $errorCode
	Check (Bytes-Equal (Read-Bytes $project) $before) "$name -> the .dxproj is unchanged"
	return $r
}

function Expect-Saved([string]$name, $r)
{
	Check ($r.Code -eq 0 -and $r.Json.success -and $null -eq $r.Json.error -and $r.Json.changed -eq $true -and $r.Json.saved -eq $true) "$name -> exit 0, changed, saved" "$($r.Code) $($r.Stdout) $($r.Stderr)"
}

function Get-Component($entity, [string]$type) { return @($entity.components | Where-Object { $_.type -eq $type })[0] }

function New-Asset([string]$file, [int]$type, [Guid]$guid)
{
	$null = New-Item -ItemType Directory -Force (Split-Path $file)
	$bw = [System.IO.BinaryWriter]::new([System.IO.File]::Create($file))
	$bw.Write([int]$type)
	$bytes = $guid.ToByteArray()
	$bw.Write([int]$bytes.Length)
	$bw.Write($bytes)
	$bw.Write([long][DateTime]::Now.ToBinary())
	$bw.Write([int]0) # hash
	$bw.Write([int]0) # icon
	$bw.Dispose()
}

if (-not (Test-Path $Cli)) { throw "dxforge.exe がありません: $Cli(./build.ps1 -Target editor -Configuration $Configuration でビルドしてください)" }

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("dxforge_ops_test_" + [Guid]::NewGuid().ToString("N"))
$null = New-Item -ItemType Directory -Force $work

# エディタのコードを呼ぶ間は、Debug.Assert の発火を FailFast ではなくテストの失敗として数える
Add-Type -AssemblyName PresentationFramework
Add-Type -TypeDefinition @'
using System.Collections.Generic;
using System.Diagnostics;
namespace DXForgeOpsTest
{
	public class AssertRecorder : TraceListener
	{
		public readonly List<string> Failures = new List<string>();
		public override void Write(string message) { }
		public override void WriteLine(string message) { }
		public override void Fail(string message, string detailMessage) { Failures.Add(message + " " + detailMessage); }
	}
}
'@
$assertRecorder = [DXForgeOpsTest.AssertRecorder]::new()
$listeners = @([System.Diagnostics.Trace]::Listeners)
[System.Diagnostics.Trace]::Listeners.Clear()
[void][System.Diagnostics.Trace]::Listeners.Add($assertRecorder)
$editor = [System.Reflection.Assembly]::LoadFrom((Join-Path $OutDir "DXForgeEditor.dll"))
$flags = [System.Reflection.BindingFlags]"Public,NonPublic,Static,Instance"
if (-not [System.Windows.Application]::Current) { $null = [System.Windows.Application]::new() }
$environment = $editor.GetType("DXForgeEditor.GameProject.ProjectLoadEnvironment", $true).GetProperty("Headless", $flags)
$environment.SetValue($null, $true)
$projectType = $editor.GetType("DXForgeEditor.GameProject.Project", $true)
$lockType = $editor.GetType("DXForgeEditor.Automation.ProjectEditorLock", $true)
function Load-Project([string]$file) { return $projectType.GetMethod("Load", $flags).Invoke($null, [object[]]@([string]$file)) }
function Set-EditorProject($project) { $null = $lockType.GetMethod("OnProjectChanged", $flags).Invoke($null, [object[]]@(, $project)) }

try
{
	Write-Host "1. arguments, --help, schema"
	$r = Invoke-Cli @("--help")
	Check ($r.Code -eq 0 -and $r.Stdout -match "--ops" -and $r.Stdout -match "--dry-run" -and $r.Stdout -match "44" -and $r.Stdout -match [regex]::Escape((Join-Path $OutDir "Schema\ops.schema.json"))) "--help shows --ops, the exit codes and the schema's location" $r.Stdout
	Check (Test-Path (Join-Path $OutDir "Schema\ops.schema.json")) "ops.schema.json is copied next to dxforge.exe"
	$op = '{"op":"list-scenes"}'
	foreach ($case in @(
			@{ Name = "--ops with --op"; Args = @("--ops", "a.json", "--op", $op) },
			@{ Name = "--op with --frames"; Args = @("--op", $op, "--frames", "1") },
			@{ Name = "--op with --export-game-bin"; Args = @("--op", $op, "--export-game-bin", "x.bin") },
			@{ Name = "--op with --scene"; Args = @("--op", $op, "--scene", "x") },
			@{ Name = "--op with --dt"; Args = @("--op", $op, "--dt", "1") },
			@{ Name = "--op with --runner"; Args = @("--op", $op, "--runner", "x.exe") },
			@{ Name = "--op with --keep-game-bin"; Args = @("--op", $op, "--keep-game-bin", "x.bin") },
			@{ Name = "--dry-run without ops"; Args = @("--dry-run", "--frames", "1") }))
	{
		$r = Invoke-Cli (@("--load-project", $TransformOnly) + $case.Args) -Json
		Check ($r.Code -eq 2 -and $r.Json.error.code -eq "invalid_arguments") "$($case.Name) -> exit 2" $r.Stdout
	}
	$r = Invoke-Cli @("--op", $op) -Json
	Check ($r.Code -eq 2 -and $r.Json.mode -eq "ops" -and $r.Json.changed -eq $false -and $r.Json.saved -eq $false) "no --load-project -> exit 2, the ops JSON" $r.Stdout
	$r = Invoke-Op (Join-Path $work "missing.dxproj") $op
	Expect-Failure "missing .dxproj" $r 30 "project_not_found"
	$broken = Join-Path $work "broken.dxproj"
	[System.IO.File]::WriteAllText($broken, "<Foo/>")
	$r = Invoke-Op $broken $op
	Expect-Failure "broken .dxproj" $r 31 "project_invalid"

	# Schema とコードの一致(操作の名前、キー、必須のキー)
	$schema = [System.IO.File]::ReadAllText($SchemaFile) | ConvertFrom-Json
	$documentType = $editor.GetType("DXForgeEditor.Automation.SceneOpDocument", $true)
	$allowed = $documentType.GetField("AllowedKeys", $flags).GetValue($null)
	$required = $documentType.GetField("RequiredKeys", $flags).GetValue($null)
	$schemaOps = @($schema.'$defs'.op.oneOf | ForEach-Object { $_.'$ref'.Split("/")[-1] })
	Check ((@($schemaOps | Sort-Object) -join ",") -eq (@($allowed.Keys | Sort-Object) -join ",")) "the schema lists the same ops as SceneOpDocument" ($schemaOps -join ",")
	foreach ($name in $schemaOps)
	{
		$def = $schema.'$defs'.$name
		$schemaKeys = @($def.properties.PSObject.Properties.Name | Where-Object { $_ -ne "op" } | Sort-Object) -join ","
		$schemaRequired = @($def.required | Where-Object { $_ -ne "op" } | Sort-Object) -join ","
		Check ($def.properties.op.const -eq $name -and $def.additionalProperties -eq $false -and $schemaKeys -eq (@($allowed[$name] | Sort-Object) -join ",") -and $schemaRequired -eq (@($required[$name] | Sort-Object) -join ",")) "schema of $name == SceneOpDocument" "$schemaKeys / $schemaRequired"
	}

	Write-Host "2. invalid ops (40)"
	$project = New-FixtureCopy
	$before = Read-Bytes $project
	# 前の操作は正しいので、形式の誤りがあれば実行の前に断っていることが分かる
	$valid = '{"op":"rename","target":"name:Origin","name":"Renamed"}'
	foreach ($case in @(
			@{ Name = "unknown op"; Op = '{"op":"nope"}'; Key = "op" },
			@{ Name = "no op"; Op = '{"target":"name:Origin"}'; Key = "op" },
			@{ Name = "op is not a string"; Op = '{"op":1}'; Key = "op" },
			@{ Name = "not an object"; Op = '[]'; Key = $null },
			@{ Name = "unknown key"; Op = '{"op":"set-transform","target":"name:Origin","posiiton":[1,2,3]}'; Key = "posiiton" },
			@{ Name = "key of another op"; Op = '{"op":"get-entity","target":"name:Origin","name":"x"}'; Key = "name" },
			@{ Name = "duplicate key"; Op = '{"op":"get-entity","target":"name:Origin","target":"name:Moved"}'; Key = "target" },
			@{ Name = "missing target"; Op = '{"op":"get-entity"}'; Key = "target" },
			@{ Name = "selector without a kind"; Op = '{"op":"get-entity","target":"Origin"}'; Key = "target" },
			@{ Name = "selector not a string"; Op = '{"op":"get-entity","target":1}'; Key = "target" },
			@{ Name = "bad guid selector"; Op = '{"op":"get-entity","target":"guid:xyz"}'; Key = "target" },
			@{ Name = "empty name selector"; Op = '{"op":"get-entity","target":"name:"}'; Key = "target" },
			@{ Name = "bad index selector"; Op = '{"op":"get-entity","target":"index:Sample Scene/x"}'; Key = "target" },
			@{ Name = "negative index selector"; Op = '{"op":"get-entity","target":"index:Sample Scene/-1"}'; Key = "target" },
			@{ Name = "undefined alias"; Op = '{"op":"get-entity","target":"$nope"}'; Key = "target" },
			@{ Name = "empty new name"; Op = '{"op":"rename","target":"name:Origin","name":" "}'; Key = "name" },
			@{ Name = "enabled not a bool"; Op = '{"op":"set-enabled","target":"name:Origin","enabled":"true"}'; Key = "enabled" },
			@{ Name = "vector too short"; Op = '{"op":"set-transform","target":"name:Origin","position":[1,2]}'; Key = "position" },
			@{ Name = "vector of strings"; Op = '{"op":"set-transform","target":"name:Origin","scale":["1","2","3"]}'; Key = "scale" },
			@{ Name = "float overflow"; Op = '{"op":"set-transform","target":"name:Origin","position":[1e39,0,0]}'; Key = "position" },
			@{ Name = "both rotations"; Op = '{"op":"set-transform","target":"name:Origin","rotation_deg":[0,0,0],"rotation_rad":[0,0,0]}'; Key = "rotation_deg" },
			@{ Name = "set-transform without values"; Op = '{"op":"set-transform","target":"name:Origin"}'; Key = $null },
			@{ Name = "unknown component type"; Op = '{"op":"add-component","target":"name:Origin","type":"mesh"}'; Key = "type" },
			@{ Name = "script without a name"; Op = '{"op":"add-component","target":"name:Origin","type":"script"}'; Key = "name" },
			@{ Name = "script with an asset"; Op = '{"op":"add-component","target":"name:Origin","type":"script","name":"A","asset":"default:cube"}'; Key = "asset" },
			@{ Name = "geometry without an asset"; Op = '{"op":"add-component","target":"name:Origin","type":"geometry"}'; Key = "asset" },
			@{ Name = "geometry with a name"; Op = '{"op":"add-component","target":"name:Origin","type":"geometry","asset":"default:cube","name":"A"}'; Key = "name" },
			@{ Name = "transform with a name"; Op = '{"op":"add-component","target":"name:Origin","type":"transform","name":"A"}'; Key = "name" },
			@{ Name = "bad asset"; Op = '{"op":"set-geometry","target":"name:Origin","asset":"cube"}'; Key = "asset" },
			@{ Name = "bad asset guid"; Op = '{"op":"set-geometry","target":"name:Origin","asset":"guid:xyz"}'; Key = "asset" },
			@{ Name = "asset path escapes Content"; Op = '{"op":"set-geometry","target":"name:Origin","asset":"path:../x.asset"}'; Key = "asset" },
			@{ Name = "absolute asset path"; Op = '{"op":"set-geometry","target":"name:Origin","asset":"path:C:/x.asset"}'; Key = "asset" },
			@{ Name = "bad alias"; Op = '{"op":"create-entity","as":"a b"}'; Key = "as" }))
	{
		$r = Expect-Unchanged $case.Name $project $before 40 "ops_invalid" { Invoke-Ops $project @($valid, $case.Op) }
		Check ($r.Json.error.op_index -eq 1 -and $r.Json.error.key -eq $case.Key -and @($r.Json.results).Count -eq 0) "$($case.Name) -> error points at ops[1].$($case.Key), nothing was run" $r.Stdout
	}
	$r = Expect-Unchanged "alias used before it is created" $project $before 40 "ops_invalid" { Invoke-Ops $project @('{"op":"get-entity","target":"$a"}', '{"op":"create-entity","as":"a"}') }
	Check ($r.Json.error.op_index -eq 0 -and $r.Json.error.key -eq "target") "alias used before it is created -> error points at ops[0].target" $r.Stdout
	$r = Expect-Unchanged "duplicate alias" $project $before 40 "ops_invalid" { Invoke-Ops $project @('{"op":"create-entity","as":"a"}', '{"op":"create-entity","as":"a"}') }
	Check ($r.Json.error.op_index -eq 1 -and $r.Json.error.key -eq "as") "duplicate alias -> error points at ops[1].as" $r.Stdout
	$opsFile = Join-Path $work "bad_ops.json"
	foreach ($case in @(
			@{ Name = "not JSON"; Text = "{"; Key = $null },
			@{ Name = "top level is an array"; Text = '[{"op":"list-scenes"}]'; Key = $null },
			@{ Name = "no ops"; Text = "{}"; Key = "ops" },
			@{ Name = "ops is not an array"; Text = '{"ops":{}}'; Key = "ops" },
			@{ Name = "unknown top-level key"; Text = '{"ops":[],"x":1}'; Key = "x" }))
	{
		[System.IO.File]::WriteAllText($opsFile, $case.Text)
		$r = Expect-Unchanged $case.Name $project $before 40 "ops_invalid" { Invoke-Cli @("--load-project", $project, "--ops", $opsFile) -Json }
		Check ($null -eq $r.Json.error.op_index -and $r.Json.error.key -eq $case.Key) "$($case.Name) -> error.key" $r.Stdout
	}
	$r = Expect-Unchanged "--ops file does not exist" $project $before 40 "ops_invalid" { Invoke-Cli @("--load-project", $project, "--ops", (Join-Path $work "none.json")) -Json }
	$r = Expect-Unchanged "--op is not JSON" $project $before 40 "ops_invalid" { Invoke-Cli @("--load-project", $project, "--op", "nope") -Json }
	[System.IO.File]::WriteAllBytes($opsFile, [byte[]](0xEF, 0xBB, 0xBF) + [System.Text.Encoding]::UTF8.GetBytes('{"$schema":"x","ops":[]}'))
	$r = Invoke-Cli @("--load-project", $project, "--ops", $opsFile) -Json
	Check ($r.Code -eq 0 -and $r.Json.op_count -eq 0 -and $r.Json.changed -eq $false -and $r.Json.saved -eq $false) "a UTF-8 BOM, `$schema and no ops are accepted" $r.Stdout

	Write-Host "3. operations"
	# 読む: 保存しない
	$project = New-FixtureCopy
	$before = Read-Bytes $project
	$timeBefore = [System.IO.File]::GetLastWriteTimeUtc($project)
	$r = Invoke-Ops $project @(
		'{"op":"list-scenes"}', '{"op":"list-entities"}', '{"op":"list-entities","scene":"Second Scene"}',
		"{`"op`":`"get-entity`",`"target`":`"guid:$MovedGuid`"}", '{"op":"get-entity","target":"name:Moved"}', '{"op":"get-entity","target":"index:Sample Scene/1"}')
	Check ($r.Code -eq 0 -and $r.Json.success -and $r.Json.mode -eq "ops" -and $r.Json.dry_run -eq $false -and $r.Json.changed -eq $false -and $r.Json.saved -eq $false -and $r.Json.op_count -eq 6) "read-only ops -> exit 0, not changed, not saved" "$($r.Stdout) $($r.Stderr)"
	Check ((Bytes-Equal (Read-Bytes $project) $before) -and [System.IO.File]::GetLastWriteTimeUtc($project) -eq $timeBefore) "read-only ops do not touch the .dxproj"
	$results = @($r.Json.results)
	$scenes = @($results[0].scenes)
	Check ($scenes.Count -eq 2 -and $scenes[0].name -eq "Sample Scene" -and $scenes[0].active -eq $true -and $scenes[0].entity_count -eq 3 -and $scenes[1].active -eq $false) "list-scenes" ($results[0] | ConvertTo-Json -Depth 5 -Compress)
	Check (((@($results[1].entities) | ForEach-Object { $_.name }) -join ",") -eq "Origin,Moved,Scaled,OnlyInSecond") "list-entities without a scene -> all scenes" ($results[1] | ConvertTo-Json -Depth 8 -Compress)
	Check (@($results[2].entities).Count -eq 1 -and @($results[2].entities)[0].name -eq "OnlyInSecond" -and @($results[2].entities)[0].scene -eq "Second Scene" -and @($results[2].entities)[0].index -eq 0) "list-entities with a scene" ($results[2] | ConvertTo-Json -Depth 8 -Compress)
	foreach ($i in 3..5)
	{
		$entity = $results[$i].entity
		$transform = Get-Component $entity "transform"
		Check ($results[$i].ok -and $results[$i].changed -eq $false -and $entity.guid -eq $MovedGuid -and $entity.name -eq "Moved" -and $entity.enabled -eq $true -and $entity.scene -eq "Sample Scene" -and $entity.index -eq 1) "get-entity selector $i finds Moved" ($results[$i] | ConvertTo-Json -Depth 8 -Compress)
		Check ((Near $transform.position @(1.5, -2, 3.25)) -and (Near $transform.rotation_rad @(0, 1.5707964, 0)) -and (Near $transform.rotation_deg @(0, 90, 0)) -and (Near $transform.scale @(1, 1, 1))) "get-entity returns the transform, rotation in both units" ($transform | ConvertTo-Json -Compress)
	}

	# 変える: 1 つずつ --op で実行して、保存され、読み直しても残る
	$r = Invoke-Op $project '{"op":"rename","target":"name:Origin","name":"Renamed"}'
	Expect-Saved "rename" $r
	Check ($r.Json.results[0].entity.name -eq "Renamed" -and $r.Json.results[0].entity.guid -eq $OriginGuid) "rename keeps the GUID" $r.Stdout
	Check (-not (Test-Path "$project.dxforge-tmp")) "no temporary file is left after saving"
	$r = Invoke-Op $project "{`"op`":`"get-entity`",`"target`":`"guid:$OriginGuid`"}"
	Check ($r.Json.results[0].entity.name -eq "Renamed") "the rename is in the saved file" $r.Stdout

	$r = Invoke-Op $project '{"op":"set-enabled","target":"name:Renamed","enabled":false}'
	Expect-Saved "set-enabled" $r
	Check ($r.Json.results[0].entity.enabled -eq $false) "set-enabled false" $r.Stdout
	$timeBefore = [System.IO.File]::GetLastWriteTimeUtc($project)
	$sameBytes = Read-Bytes $project
	$r = Invoke-Op $project '{"op":"set-enabled","target":"name:Renamed","enabled":false}'
	Check ($r.Code -eq 0 -and $r.Json.changed -eq $false -and $r.Json.saved -eq $false -and $r.Json.results[0].changed -eq $false -and (Bytes-Equal (Read-Bytes $project) $sameBytes) -and [System.IO.File]::GetLastWriteTimeUtc($project) -eq $timeBefore) "an op that changes nothing -> changed false, not saved" $r.Stdout

	$r = Invoke-Op $project '{"op":"set-transform","target":"name:Renamed","position":[1,2,3],"rotation_deg":[0,90,45],"scale":[2,2,2]}'
	Expect-Saved "set-transform (degrees)" $r
	$transform = Get-Component $r.Json.results[0].entity "transform"
	Check ((Near $transform.position @(1, 2, 3)) -and (Near $transform.rotation_deg @(0, 90, 45)) -and (Near $transform.rotation_rad @(0, ([Math]::PI / 2), ([Math]::PI / 4))) -and (Near $transform.scale @(2, 2, 2))) "rotation_deg is stored as radians, both are returned" ($transform | ConvertTo-Json -Compress)
	$r = Invoke-Op $project '{"op":"set-transform","target":"name:Renamed","rotation_rad":[1,0,0]}'
	Expect-Saved "set-transform (radians, only one value)" $r
	$transform = Get-Component $r.Json.results[0].entity "transform"
	Check ((Near $transform.rotation_rad @(1, 0, 0)) -and (Near $transform.rotation_deg @((180 / [Math]::PI), 0, 0)) -and (Near $transform.position @(1, 2, 3)) -and (Near $transform.scale @(2, 2, 2))) "rotation_rad keeps the other values" ($transform | ConvertTo-Json -Compress)

	$r = Invoke-Op $project '{"op":"add-component","target":"name:Renamed","type":"script","name":"Mover"}'
	Expect-Saved "add-component script" $r
	Check ((Get-Component $r.Json.results[0].entity "script").name -eq "Mover" -and @($r.Json.results[0].warnings).Count -eq 1 -and "$($r.Json.results[0].warnings)" -match "Mover.*未確認" -and $r.Stderr -match "\[warning\].*Mover.*未確認") "the script is added, with an unverified warning in the result and stderr" "$($r.Stdout) $($r.Stderr)"
	$r = Invoke-Op $project '{"op":"remove-component","target":"name:Renamed","type":"script"}'
	Expect-Saved "remove-component" $r
	Check ($null -eq (Get-Component $r.Json.results[0].entity "script")) "the script is removed" $r.Stdout

	# アセット: default:cube, guid:, path:
	$meshGuid = [Guid]::NewGuid()
	$otherMeshGuid = [Guid]::NewGuid()
	$textureGuid = [Guid]::NewGuid()
	$content = Join-Path (Split-Path $project) "Content"
	New-Asset (Join-Path $content "Meshes\Sub\A.asset") 4 $meshGuid
	New-Asset (Join-Path $content "B.asset") 4 $otherMeshGuid
	New-Asset (Join-Path $content "T.asset") 6 $textureGuid
	[System.IO.File]::WriteAllText((Join-Path $content "broken.asset"), "x")
	$r = Invoke-Op $project '{"op":"add-component","target":"name:Renamed","type":"geometry","asset":"default:cube"}'
	Expect-Saved "add-component geometry default:cube" $r
	Check ((Get-Component $r.Json.results[0].entity "geometry").geometry -eq $DefaultCubeGuid -and @($r.Json.results[0].warnings).Count -eq 0) "default:cube is the fixed GUID" $r.Stdout
	$r = Invoke-Op $project '{"op":"set-geometry","target":"name:Renamed","asset":"path:Meshes/Sub/A.asset"}'
	Expect-Saved "set-geometry path:" $r
	Check ((Get-Component $r.Json.results[0].entity "geometry").geometry -eq "$meshGuid") "path: reads the asset's GUID (a broken .asset next to it is ignored)" $r.Stdout
	$r = Invoke-Op $project "{`"op`":`"set-geometry`",`"target`":`"name:Renamed`",`"asset`":`"guid:$otherMeshGuid`"}"
	Expect-Saved "set-geometry guid:" $r
	Check ((Get-Component $r.Json.results[0].entity "geometry").geometry -eq "$otherMeshGuid") "guid: finds the asset in the project's Content" $r.Stdout
	$r = Invoke-Op $project "{`"op`":`"set-geometry`",`"target`":`"name:Renamed`",`"asset`":`"guid:$otherMeshGuid`"}"
	Check ($r.Code -eq 0 -and $r.Json.changed -eq $false -and $r.Json.saved -eq $false) "set-geometry to the same asset -> not changed" $r.Stdout
	$r = Invoke-Op $project "{`"op`":`"set-geometry`",`"target`":`"name:Renamed`",`"asset`":`"guid:$DefaultCubeGuid`"}"
	Expect-Saved "set-geometry guid: of the default cube" $r

	# create-entity, 別名, remove-entity
	$r = Invoke-Ops $project @(
		'{"op":"create-entity","as":"made","name":"Made"}', '{"op":"set-transform","target":"$made","position":[5,6,7]}',
		'{"op":"create-entity","as":"second","scene":"Second Scene"}', '{"op":"rename","target":"$second","name":"Second Made"}',
		'{"op":"remove-entity","target":"name:Moved"}')
	Expect-Saved "create-entity with aliases and remove-entity" $r
	$results = @($r.Json.results)
	Check ($results[0].entity.scene -eq "Sample Scene" -and $results[0].entity.name -eq "Made" -and $results[0].entity.guid -match "^[0-9a-f-]{36}$" -and $results[0].entity.guid -ne "00000000-0000-0000-0000-000000000000" -and $results[0].entity.index -eq 3) "create-entity returns the GUID, default scene is the active one" ($results[0] | ConvertTo-Json -Depth 8 -Compress)
	Check ($results[2].entity.scene -eq "Second Scene" -and $results[2].entity.name -eq "Empty Entity" -and $results[3].entity.guid -eq $results[2].entity.guid) "create-entity with scene and default name, aliases point at the same entity" ($results[2] | ConvertTo-Json -Depth 8 -Compress)
	Check ($results[4].entity.guid -eq $MovedGuid -and $results[4].changed -eq $true) "remove-entity returns the removed entity" ($results[4] | ConvertTo-Json -Depth 8 -Compress)
	$made = $results[0].entity.guid
	$r = Invoke-Op $project '{"op":"list-entities","scene":"Sample Scene"}'
	Check (((@($r.Json.results[0].entities) | ForEach-Object { $_.name }) -join ",") -eq "Renamed,Scaled,Made") "the saved file has the new and removed entities" $r.Stdout
	$r = Invoke-Op $project "{`"op`":`"get-entity`",`"target`":`"guid:$made`"}"
	Check ($r.Code -eq 0 -and (Near (Get-Component $r.Json.results[0].entity "transform").position @(5, 6, 7))) "a created entity keeps its GUID in the saved file" $r.Stdout

	Write-Host "4. not found (41), operation failed (42)"
	$before = Read-Bytes $project
	foreach ($case in @(
			@{ Name = "guid: not found"; Op = '{"op":"get-entity","target":"guid:11111111-1111-4111-8111-111111111111"}'; Code = "target_not_found" },
			@{ Name = "name: not found"; Op = '{"op":"rename","target":"name:Nope","name":"x"}'; Code = "target_not_found" },
			@{ Name = "index out of range"; Op = '{"op":"remove-entity","target":"index:Sample Scene/99"}'; Code = "target_not_found" },
			@{ Name = "index: scene not found"; Op = '{"op":"get-entity","target":"index:Nope/0"}'; Code = "scene_not_found" },
			@{ Name = "list-entities scene not found"; Op = '{"op":"list-entities","scene":"Nope"}'; Code = "scene_not_found" },
			@{ Name = "create-entity scene not found"; Op = '{"op":"create-entity","scene":"Nope"}'; Code = "scene_not_found" },
			@{ Name = "asset guid not found"; Op = '{"op":"set-geometry","target":"name:Renamed","asset":"guid:11111111-1111-4111-8111-111111111111"}'; Code = "asset_not_found" },
			@{ Name = "asset path not found"; Op = '{"op":"set-geometry","target":"name:Renamed","asset":"path:Nope.asset"}'; Code = "asset_not_found" }))
	{
		$r = Expect-Unchanged $case.Name $project $before 41 $case.Code { Invoke-Op $project $case.Op }
		Check ($r.Json.error.op_index -eq 0 -and $r.Json.results[0].ok -eq $false -and $r.Json.results[0].error.code -eq $case.Code) "$($case.Name) -> the failed op has its reason" $r.Stdout
	}
	$r = Expect-Unchanged "removed alias" $project $before 41 "target_not_found" { Invoke-Ops $project @('{"op":"create-entity","as":"a"}', '{"op":"remove-entity","target":"$a"}', '{"op":"get-entity","target":"$a"}') }
	Check ($r.Json.error.op_index -eq 2) "removed alias -> error.op_index" $r.Stdout
	$r = Expect-Unchanged "ambiguous name" $project $before 41 "ambiguous_target" { Invoke-Ops $project @('{"op":"create-entity","name":"Dup"}', '{"op":"create-entity","name":"Dup"}', '{"op":"remove-entity","target":"name:Dup"}') }
	Check ($r.Stdout -match "guid:") "ambiguous name -> the message suggests guid:" $r.Stdout
	$duplicateGuid = New-FixtureCopy
	$xml = [System.IO.File]::ReadAllText($duplicateGuid).Replace($MovedGuid, $OriginGuid)
	[System.IO.File]::WriteAllText($duplicateGuid, $xml)
	$r = Invoke-Op $duplicateGuid "{`"op`":`"get-entity`",`"target`":`"guid:$OriginGuid`"}"
	Expect-Failure "guid: matching two entities in the file" $r 41 "ambiguous_target"
	# シーンの名前の重複は禁じられていない。index: や scene で先頭のシーンを選ばない
	$duplicateScene = New-EmptyProject "DuplicateScene"
	$sceneXml = [System.IO.File]::ReadAllText($duplicateScene)
	$secondScene = '<Scene z:Id="i3"><GameEntities xmlns:a="http://schemas.datacontract.org/2004/07/DXForgeEditor.Components"/><IsActive>false</IsActive><Name>Sample Scene</Name><Project z:Ref="i1"/></Scene></Scenes>'
	[System.IO.File]::WriteAllText($duplicateScene, $sceneXml.Replace("</Scenes>", $secondScene))
	Expect-Saved "create in the active scene of a project with two scenes of one name" (Invoke-Op $duplicateScene '{"op":"create-entity","name":"Keep"}')
	$duplicateBefore = Read-Bytes $duplicateScene
	foreach ($case in @(
			@{ Name = "index: with two scenes of one name"; Op = '{"op":"remove-entity","target":"index:Sample Scene/0"}'; Key = "scene" },
			@{ Name = "list-entities with two scenes of one name"; Op = '{"op":"list-entities","scene":"Sample Scene"}'; Key = "scene" },
			@{ Name = "create-entity with two scenes of one name"; Op = '{"op":"create-entity","scene":"Sample Scene"}'; Key = "scene" }))
	{
		$r = Expect-Unchanged $case.Name $duplicateScene $duplicateBefore 41 "ambiguous_target" { Invoke-Op $duplicateScene $case.Op }
		Check ($r.Json.error.key -eq $case.Key) "$($case.Name) -> error.key" $r.Stdout
	}
	$r = Invoke-Op $duplicateScene '{"op":"get-entity","target":"name:Keep"}'
	Check ($r.Code -eq 0 -and $r.Json.results[0].entity.name -eq "Keep") "name: still finds the entity when two scenes share a name" $r.Stdout
	foreach ($case in @(
			@{ Name = "add a Transform it already has"; Op = '{"op":"add-component","target":"name:Renamed","type":"transform"}'; Code = "component_exists" },
			@{ Name = "add a Geometry it already has"; Op = '{"op":"add-component","target":"name:Renamed","type":"geometry","asset":"default:cube"}'; Code = "component_exists" },
			@{ Name = "remove the Transform"; Op = '{"op":"remove-component","target":"name:Renamed","type":"transform"}'; Code = "transform_not_removable" },
			@{ Name = "remove a component it does not have"; Op = '{"op":"remove-component","target":"name:Renamed","type":"script"}'; Code = "component_not_found" },
			@{ Name = "set-geometry without a Geometry"; Op = '{"op":"set-geometry","target":"name:Scaled","asset":"default:cube"}'; Code = "component_not_found" },
			@{ Name = "asset that is not a mesh (guid:)"; Op = "{`"op`":`"set-geometry`",`"target`":`"name:Renamed`",`"asset`":`"guid:$textureGuid`"}"; Code = "asset_not_mesh" },
			@{ Name = "asset that is not a mesh (path:)"; Op = '{"op":"set-geometry","target":"name:Renamed","asset":"path:T.asset"}'; Code = "asset_not_mesh" }))
	{
		$r = Expect-Unchanged $case.Name $project $before 42 $case.Code { Invoke-Op $project $case.Op }
	}

	Write-Host "5. --dry-run, failures part way, save failure (43)"
	$project = New-FixtureCopy
	$before = Read-Bytes $project
	$timeBefore = [System.IO.File]::GetLastWriteTimeUtc($project)
	$r = Invoke-Ops $project @('{"op":"create-entity","as":"a","name":"DryRun"}', '{"op":"set-transform","target":"$a","position":[1,2,3]}', '{"op":"get-entity","target":"name:DryRun"}') -DryRun
	Check ($r.Code -eq 0 -and $r.Json.dry_run -eq $true -and $r.Json.changed -eq $true -and $r.Json.saved -eq $false -and $r.Json.results[2].entity.name -eq "DryRun") "--dry-run runs the ops in memory -> changed, not saved" "$($r.Stdout) $($r.Stderr)"
	Check ($r.Stderr -match "保存していません") "--dry-run with changes says on stderr that nothing was saved" $r.Stderr
	Check ((Bytes-Equal (Read-Bytes $project) $before) -and [System.IO.File]::GetLastWriteTimeUtc($project) -eq $timeBefore -and -not (Test-Path "$project.dxforge-tmp")) "--dry-run does not touch the .dxproj"
	$r = Invoke-Op $project '{"op":"list-scenes"}' -DryRun
	Check ($r.Code -eq 0 -and $r.Json.changed -eq $false -and -not ($r.Stderr -match "保存していません")) "--dry-run of a read-only op has no notice" "$($r.Stdout) $($r.Stderr)"
	$r = Invoke-Op $project '{"op":"rename","target":"name:Nope","name":"x"}' -DryRun
	Expect-Failure "--dry-run reports a failure too" $r 41 "target_not_found"

	$r = Expect-Unchanged "failure part way (41)" $project $before 41 "target_not_found" { Invoke-Ops $project @('{"op":"create-entity","name":"X"}', '{"op":"rename","target":"name:Origin","name":"Y"}', '{"op":"get-entity","target":"name:Nope"}', '{"op":"remove-entity","target":"name:Moved"}') }
	$results = @($r.Json.results)
	Check ($results.Count -eq 3 -and $results[0].ok -and $results[1].ok -and $results[2].ok -eq $false -and $r.Json.error.op_index -eq 2 -and $r.Json.op_count -eq 4 -and $r.Json.error.message -match "ops\[2\]") "failure part way -> results stop at the failed op, with its number and reason" $r.Stdout
	$r = Expect-Unchanged "failure part way (42)" $project $before 42 "transform_not_removable" { Invoke-Ops $project @('{"op":"rename","target":"name:Origin","name":"Y"}', '{"op":"remove-component","target":"name:Moved","type":"transform"}') }
	Check (-not (Test-Path "$project.dxforge-tmp")) "a failure part way leaves no temporary file"

	# 一時ファイルの場所をフォルダで塞ぐと、保存できない
	$null = New-Item -ItemType Directory "$project.dxforge-tmp"
	$r = Expect-Unchanged "save failure" $project $before 43 "save_failed" { Invoke-Op $project '{"op":"rename","target":"name:Origin","name":"Y"}' }
	Remove-Item "$project.dxforge-tmp"
	$r = Invoke-Op $project '{"op":"list-scenes"}'
	Check ($r.Code -eq 0) "the project still loads after the save failure" $r.Stdout

	Write-Host "6. editor has the project open (44)"
	$project = New-EmptyProject "Locked"
	$other = New-EmptyProject "Other"
	$lockFile = Join-Path (Split-Path $project) ".DXForge\editor.lock"
	$before = Read-Bytes $project
	$change = '{"op":"create-entity"}'
	$loaded = (Load-Project $project).PSObject.BaseObject
	Set-EditorProject $loaded
	Check (Test-Path $lockFile) "the editor's lock creates .DXForge\editor.lock"
	$null = Expect-Unchanged "change while the editor has it open" $project $before 44 "editor_open" { Invoke-Op $project $change }
	$null = Expect-Unchanged "--dry-run change while the editor has it open" $project $before 44 "editor_open" { Invoke-Op $project $change -DryRun }
	$null = Expect-Unchanged "read and change while the editor has it open" $project $before 44 "editor_open" { Invoke-Ops $project @('{"op":"list-scenes"}', $change) }
	$r = Invoke-Op $project '{"op":"list-scenes"}'
	Check ($r.Code -eq 0 -and $r.Json.saved -eq $false) "read-only ops pass while the editor has it open" $r.Stdout
	$r = Expect-Unchanged "40 comes before 44" $project $before 40 "ops_invalid" { Invoke-Op $project '{"op":"nope"}' }
	$r = Invoke-Op $other $change
	Expect-Saved "another project is not locked" $r
	# 別の場所に写した .dxproj でも、中の Path がエディタの開いているフォルダならば断る
	$moved = Join-Path $work "moved\"
	$null = New-Item -ItemType Directory -Force $moved
	$movedFile = Join-Path $moved "Locked.dxproj"
	[System.IO.File]::Copy($project, $movedFile)
	$null = Expect-Unchanged "a copy whose saved Path is the open folder" $movedFile $before 44 "editor_open" { Invoke-Op $movedFile $change }
	# エディタがプロジェクトを切り替える
	$otherLoaded = (Load-Project $other).PSObject.BaseObject
	Set-EditorProject $otherLoaded
	Check (-not (Test-Path $lockFile)) "switching projects releases and deletes the old lock"
	$r = Invoke-Op $project $change
	Expect-Saved "after switching, the old project can be changed" $r
	$otherBefore = Read-Bytes $other
	$null = Expect-Unchanged "after switching, the new project is refused" $other $otherBefore 44 "editor_open" { Invoke-Op $other $change }
	# エディタが閉じる
	Set-EditorProject $null
	Check (-not (Test-Path (Join-Path (Split-Path $other) ".DXForge\editor.lock"))) "closing the project deletes the lock"
	$r = Invoke-Op $other $change
	Expect-Saved "after closing, the project can be changed" $r
	# 残ったロックファイルは誰も開いていなければ関係ない。プロセスの異常終了は OS が閉じるので、この形になる
	$null = New-Item -ItemType Directory -Force (Split-Path $lockFile)
	[System.IO.File]::WriteAllText($lockFile, "")
	$r = Invoke-Op $project $change
	Expect-Saved "a leftover lock file that nobody holds is ignored" $r
	# 同じプロジェクトを 2 つのエディタが開く。先に閉じたほうが、残っているエディタのロックを消さない
	$lockFolder = Split-Path $project
	function Is-Held { return [bool]$lockType.GetMethod("IsHeld", $flags).Invoke($null, [object[]]@([string]$lockFolder)) }
	function Acquire-Lock { return $lockType.GetMethod("AcquireHandle", $flags).Invoke($null, [object[]]@([string]$lockFolder)).PSObject.BaseObject }
	function Release-Lock($handle) { $null = $lockType.GetMethod("ReleaseHandle", $flags).Invoke($null, [object[]]@(, $handle)) }
	$first = Acquire-Lock
	$second = Acquire-Lock
	Check (Is-Held) "two editors can hold the same project"
	$before = Read-Bytes $project
	Release-Lock $first
	Check ((Is-Held) -and (Test-Path $lockFile)) "closing one of two editors keeps the lock"
	$null = Expect-Unchanged "change while the second editor still has it open" $project $before 44 "editor_open" { Invoke-Op $project $change }
	Release-Lock $second
	Check (-not (Is-Held) -and -not (Test-Path $lockFile)) "closing both editors releases and deletes the lock"
	$r = Invoke-Op $project $change
	Expect-Saved "after closing both editors, the project can be changed" $r
	# 片方が落ちても(ハンドルだけが閉じる)、残りのエディタのロックは効き続け、最後に閉じるほうが消す
	$first = Acquire-Lock
	$second = Acquire-Lock
	$first.Dispose()
	Check (Is-Held) "a crashed editor does not release the other editor's lock"
	Release-Lock $second
	Check (-not (Is-Held) -and -not (Test-Path $lockFile)) "the last editor deletes the lock after the other one crashed"
	Check (-not (Is-Held)) "no lock file -> not held"
	$noLockFolder = Join-Path $work "NoDXForgeFolder"
	$null = New-Item -ItemType Directory -Force $noLockFolder
	Check (-not [bool]$lockType.GetMethod("IsHeld", $flags).Invoke($null, [object[]]@([string]$noLockFolder))) "no .DXForge folder -> not held"
	# 権限がなくて確かめられないときは、上書きを防ぐ側に倒す。File.Exists が false を返す形(フォルダの一覧とファイルの属性の拒否)で試す
	[System.IO.File]::WriteAllText($lockFile, "")
	$lockFileInfo = [System.IO.FileInfo]::new($lockFile)
	$lockFolderInfo = [System.IO.DirectoryInfo]::new((Split-Path $lockFile))
	$me = [System.Security.Principal.WindowsIdentity]::GetCurrent().User
	$denyFile = [System.Security.AccessControl.FileSystemAccessRule]::new($me, [System.Security.AccessControl.FileSystemRights]"ReadData,ReadAttributes", "Deny")
	$denyFolder = [System.Security.AccessControl.FileSystemAccessRule]::new($me, [System.Security.AccessControl.FileSystemRights]"ListDirectory", "Deny")
	$before = Read-Bytes $project
	try
	{
		$deniedFile = [System.IO.FileSystemAclExtensions]::GetAccessControl($lockFileInfo)
		$deniedFile.AddAccessRule($denyFile)
		[System.IO.FileSystemAclExtensions]::SetAccessControl($lockFileInfo, $deniedFile)
		$deniedFolder = [System.IO.FileSystemAclExtensions]::GetAccessControl($lockFolderInfo)
		$deniedFolder.AddAccessRule($denyFolder)
		[System.IO.FileSystemAclExtensions]::SetAccessControl($lockFolderInfo, $deniedFolder)
		Check (-not [System.IO.File]::Exists($lockFile)) "(the denied lock file is what File.Exists cannot see)"
		Check (Is-Held) "a lock file that cannot be checked -> held"
		$null = Expect-Unchanged "change when the lock file cannot be checked" $project $before 44 "editor_open" { Invoke-Op $project $change }
	}
	finally
	{
		# NOTE: SetAccessControl は変更のないオブジェクトを書き込まないので、取っておいた ACL では戻らない。付けた規則を外す
		$restoredFolder = [System.IO.FileSystemAclExtensions]::GetAccessControl($lockFolderInfo)
		$null = $restoredFolder.RemoveAccessRule($denyFolder)
		[System.IO.FileSystemAclExtensions]::SetAccessControl($lockFolderInfo, $restoredFolder)
		$restoredFile = [System.IO.FileSystemAclExtensions]::GetAccessControl($lockFileInfo)
		$null = $restoredFile.RemoveAccessRule($denyFile)
		[System.IO.FileSystemAclExtensions]::SetAccessControl($lockFileInfo, $restoredFile)
	}
	Check (-not (Is-Held)) "after the ACL is restored, the leftover lock file is not held"
	[System.IO.File]::Delete($lockFile)
	# .dxproj の Path のフォルダがなければ、エディタは古い場所にフォルダを作らない
	$stale = (Load-Project $TransformOnly).PSObject.BaseObject
	$stale.Path = Join-Path $work "does_not_exist\"
	Set-EditorProject $stale
	Check (-not (Test-Path $stale.Path)) "the lock does not create a project folder that is gone"
	Set-EditorProject $null

	Write-Host "7. create in an empty project, load with the editor's code, run"
	$project = New-EmptyProject "Created"
	$r = Invoke-Op $project '{"op":"list-entities"}'
	Check ($r.Code -eq 0 -and @($r.Json.results[0].entities).Count -eq 0) "the template project has no entities" $r.Stdout
	$opsFile = Write-OpsFile @(
		'{"op":"create-entity","as":"box","name":"Box"}',
		'{"op":"set-transform","target":"$box","position":[1.5,-2,3.25],"rotation_deg":[0,90,0],"scale":[2,3,0.5]}',
		'{"op":"add-component","target":"$box","type":"script","name":"Mover"}',
		'{"op":"add-component","target":"$box","type":"geometry","asset":"default:cube"}')
	$r = Invoke-Cli @("--load-project", $project, "--ops", $opsFile) -Json
	Expect-Saved "the ops" $r
	$created = @($r.Json.results)[3].entity
	$loaded = (Load-Project $project).PSObject.BaseObject
	$entities = @($loaded.ActiveScene.GameEntities)
	Check ($entities.Count -eq 1) "Project.Load finds the entity" $entities.Count
	$entity = $entities[0]
	$transformType = $editor.GetType("DXForgeEditor.Components.Transform", $true)
	$scriptType = $editor.GetType("DXForgeEditor.Components.Script", $true)
	$geometryType = $editor.GetType("DXForgeEditor.Components.Geometry", $true)
	$transform = @($entity.Components | Where-Object { $_.GetType() -eq $transformType })[0]
	$script = @($entity.Components | Where-Object { $_.GetType() -eq $scriptType })[0]
	$geometry = @($entity.Components | Where-Object { $_.GetType() -eq $geometryType })[0]
	Check ($entity.Name -eq "Box" -and "$($entity.EntityGuid)" -eq $created.guid -and $entity.IsEnabled -and $entity.EntityId -eq -1 -and [object]::ReferenceEquals($entity.ParentScene, $loaded.ActiveScene)) "Project.Load: name, GUID, enabled, scene" "$($entity.Name) $($entity.EntityGuid)"
	Check ((Near @($transform.Position.X, $transform.Position.Y, $transform.Position.Z) @(1.5, -2, 3.25)) -and (Near @($transform.Rotation.X, $transform.Rotation.Y, $transform.Rotation.Z) @(0, ([Math]::PI / 2), 0)) -and (Near @($transform.Scale.X, $transform.Scale.Y, $transform.Scale.Z) @(2, 3, 0.5))) "Project.Load: Transform" "$($transform.Position) $($transform.Rotation) $($transform.Scale)"
	Check ($script.Name -eq "Mover" -and "$($geometry.GeometryGuid)" -eq $DefaultCubeGuid -and "$($editor.GetType('DXForgeEditor.Content.DefaultAssets', $true).GetField('DefaultGeometryGuid', $flags).GetValue($null))" -eq $DefaultCubeGuid) "Project.Load: Script and Geometry (DefaultGeometryGuid)" "$($script.Name) $($geometry.GeometryGuid)"
	$r = Invoke-Op $project "{`"op`":`"get-entity`",`"target`":`"guid:$($created.guid)`"}"
	Check ((($r.Json.results[0].entity | ConvertTo-Json -Depth 8 -Compress)) -eq ($created | ConvertTo-Json -Depth 8 -Compress)) "get-entity after saving == the result of the last op" $r.Stdout

	$r = Invoke-Cli @("--load-project", $project, "--frames", "1", "--runner", $Cli) -Json
	Check ($r.Code -eq 33) "a scene with a script cannot be run in this version (33, unchanged)" $r.Stdout
	$r = Invoke-Op $project '{"op":"remove-component","target":"name:Box","type":"script"}'
	Expect-Saved "remove the script before running" $r
	if (-not (Test-Path $Runner))
	{
		if ($RequireRunner) { Check $false "runner exists: $Runner" }
		else
		{
			Write-Host "  SKIP run case (no runner at $Runner)"
			$skips.Add("run case")
		}
	}
	else
	{
		$r = Invoke-Cli @("--load-project", $project, "--frames", "1", "--runner", $Runner) -Json
		Check ($r.Code -eq 0 -and $r.Json.entity_count -eq 1 -and @($r.Json.entities)[0].name -eq "Box" -and (Near @($r.Json.entities)[0].position @(1.5, -2, 3.25)) -and (Near @($r.Json.entities)[0].scale @(2, 3, 0.5))) "--frames runs the created entity, --json shows it" "$($r.Stdout) $($r.Stderr)"
	}

	Write-Host "8. .dxproj without GUIDs, game.bin, Japanese names"
	$noGuid = New-FixtureCopy
	$xml = [regex]::Replace([System.IO.File]::ReadAllText($noGuid), "<a:Guid>[^<]*</a:Guid>", "")
	[System.IO.File]::WriteAllText($noGuid, $xml)
	$before = Read-Bytes $noGuid
	$first = Invoke-Op $noGuid '{"op":"get-entity","target":"name:Moved"}'
	$second = Invoke-Op $noGuid '{"op":"get-entity","target":"name:Moved"}'
	Check ($first.Code -eq 0 -and $first.Json.saved -eq $false -and $first.Json.results[0].entity.guid -ne "00000000-0000-0000-0000-000000000000" -and $first.Json.results[0].entity.guid -ne $second.Json.results[0].entity.guid -and (Bytes-Equal (Read-Bytes $noGuid) $before)) "no <Guid>: a GUID is made when reading, and differs until saved" $first.Stdout
	$r = Invoke-Op $noGuid '{"op":"set-enabled","target":"name:Origin","enabled":false}'
	Expect-Saved "no <Guid>: a change saves" $r
	$first = Invoke-Op $noGuid '{"op":"get-entity","target":"name:Moved"}'
	$second = Invoke-Op $noGuid '{"op":"get-entity","target":"name:Moved"}'
	Check ($first.Json.results[0].entity.guid -eq $second.Json.results[0].entity.guid) "no <Guid>: after saving, the GUIDs are fixed" $first.Stdout

	# 保存し直しても game.bin は同じ(GUID を書かない)。値を戻して保存したあとも
	$project = New-FixtureCopy
	$binBefore = Join-Path $work "bin_before.bin"
	$binAfter = Join-Path $work "bin_after.bin"
	$r = Invoke-Cli @("--load-project", $project, "--export-game-bin", $binBefore) -Json
	Check ($r.Code -eq 0) "game.bin before saving" $r.Stdout
	$r = Invoke-Ops $project @('{"op":"rename","target":"name:Origin","name":"Temp"}', '{"op":"rename","target":"name:Temp","name":"Origin"}', '{"op":"set-enabled","target":"name:Origin","enabled":false}', '{"op":"set-enabled","target":"name:Origin","enabled":true}')
	Expect-Saved "resave" $r
	$r = Invoke-Cli @("--load-project", $project, "--export-game-bin", $binAfter) -Json
	Check ($r.Code -eq 0 -and (Bytes-Equal (Read-Bytes $binBefore) (Read-Bytes $binAfter))) "saving with dxforge does not change the game.bin" $r.Stdout

	# 日本語のパスと名前
	$jpDir = Join-Path $work "日本語のプロジェクト"
	$null = New-Item -ItemType Directory -Force $jpDir
	$jp = Join-Path $jpDir "テスト.dxproj"
	[System.IO.File]::Copy($TransformOnly, $jp)
	$r = Invoke-Ops $jp @('{"op":"create-entity","name":"立方体","as":"a"}', '{"op":"add-component","target":"$a","type":"geometry","asset":"default:cube"}')
	Expect-Saved "Japanese path and name" $r
	$r = Invoke-Op $jp '{"op":"get-entity","target":"name:立方体"}'
	Check ($r.Code -eq 0 -and $r.Json.project -eq $jp -and $r.Json.results[0].entity.name -eq "立方体" -and $r.Stdout -match "立方体") "Japanese name through --op and JSON output (not \uXXXX)" $r.Stdout

	# JSON なしの出力
	$r = Invoke-Cli @("--load-project", $TransformOnly, "--op", '{"op":"get-entity","target":"name:Moved"}')
	Check ($r.Code -eq 0 -and $r.Stdout -match "dxforge: OK, 1 ops, changed: False, saved: False" -and $r.Stdout -match "Moved") "human-readable output" $r.Stdout
	$r = Invoke-Cli @("--load-project", $TransformOnly, "--op", '{"op":"nope"}')
	Check ($r.Code -eq 40 -and $r.Stdout -match "FAILED \(exit code 40\) ops_invalid") "human-readable error" $r.Stdout

	Check ($assertRecorder.Failures.Count -eq 0) "no Debug.Assert fired in the editor code" ($assertRecorder.Failures -join "; ")
}
finally
{
	try { Set-EditorProject $null } catch { }
	$environment.SetValue($null, $false)
	[System.Diagnostics.Trace]::Listeners.Clear()
	[System.Diagnostics.Trace]::Listeners.AddRange($listeners)
	Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}

Write-Host ""
if ($skips.Count) { Write-Host "Skipped: $($skips.Count)" }
if ($failures.Count)
{
	Write-Host "FAILED: $($failures.Count)"
	$failures | ForEach-Object { Write-Host "  - $_" }
	exit 1
}
Write-Host "All ops_cli_test checks passed."
exit 0
