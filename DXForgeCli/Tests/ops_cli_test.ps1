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
      9. シーンの追加・削除・切り替え(最後のシーン、アクティブなシーン、消したシーンの別名、新しいアクティブのシーンの game.bin)。
     10. 偽の importer(fake_importer.cmd)で import-asset: 成功、folder、--dry-run、失敗で Content と .dxproj が変わらないこと、終了コードの対応、衝突、44。
     11. コミットの失敗: Content への移動の途中の失敗、保存の失敗、戻す処理の失敗(LeftoverPaths)、タイムアウト。
     12. 実物の DXForgeImport(--importer なし): box.fbx と埋め込みテクスチャの FBX を取り込み → set-geometry → --export-game-bin。
         LOD グループが 2 つの FBX で、複数の .asset が全部 assets に出て Content に移ること。
         .asset の生のバイトに一時フォルダのパスが残らないこと。ContentTools.dll(FBX SDK)がなければ SKIP。
     13. 強制終了で残った一時フォルダの片付け: ロックが空いていれば消す、使用中とロックのないものは消さない。dxforge だけを kill しても importer が生きている間は消さない。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。

.PARAMETER Runner
    ヘッドレスのランナー。既定は dxforge と同じく x64\<Debug|Release>\EngineUnitTests.exe。

.PARAMETER RequireRunner
    ランナーがないときに、実行のケースを SKIP にせず失敗にする(CI はこれを付ける)。

.PARAMETER RequireRealImporter
    実物の importer(ContentTools.dll と FbxFile)がないときに、節 12 を SKIP にせず失敗にする。CI には FBX SDK がないので付けない。

.PARAMETER FbxFile
    節 12 で取り込む ASCII の FBX。既定は FBX SDK のサンプルの box.fbx。

.PARAMETER LodFbxFile
    節 12 で取り込む、LOD のある FBX。指定しなければ、この確認は行わない。

.PARAMETER MultiLodFbxFile
    節 12 で取り込む、LOD グループが 2 つ以上ある FBX。assets に全部が出て、Content に移ることを確かめる。
    指定しなければ FbxFile から DXForgeImport\Tests\make_multilod_fbx.py で作る。

.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeCli/Tests/ops_cli_test.ps1 -RequireRunner

.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeCli/Tests/ops_cli_test.ps1 -RequireRunner -RequireRealImporter -LodFbxFile F:\DXForgeTestAssets\LODTest.fbx
#>
[CmdletBinding()]
param(
	[ValidateSet("DebugEditor", "ReleaseEditor")]
	[string]$Configuration = "DebugEditor",
	[string]$Runner,
	[switch]$RequireRunner,
	[switch]$RequireRealImporter,
	[string]$FbxFile = "C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\samples\ConvertScene\box.fbx",
	[string]$LodFbxFile,
	[string]$MultiLodFbxFile
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
function Invoke-Ops([string]$project, [string[]]$ops, [switch]$DryRun, [string]$Importer)
{
	$arguments = @("--load-project", $project, "--ops", (Write-OpsFile $ops))
	if ($DryRun) { $arguments += "--dry-run" }
	if ($Importer) { $arguments += @("--importer", $Importer) }
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

function Test-TemporaryFiles([string]$project) { return @(Get-ChildItem -LiteralPath (Split-Path $project) -Filter "*.dxforge-tmp").Count -gt 0 }

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
	Check (-not (Test-TemporaryFiles $project)) "no temporary file is left after saving"
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
	Check ((Bytes-Equal (Read-Bytes $project) $before) -and [System.IO.File]::GetLastWriteTimeUtc($project) -eq $timeBefore -and -not (Test-TemporaryFiles $project)) "--dry-run does not touch the .dxproj"
	$r = Invoke-Op $project '{"op":"list-scenes"}' -DryRun
	Check ($r.Code -eq 0 -and $r.Json.changed -eq $false -and -not ($r.Stderr -match "保存していません")) "--dry-run of a read-only op has no notice" "$($r.Stdout) $($r.Stderr)"
	$r = Invoke-Op $project '{"op":"rename","target":"name:Nope","name":"x"}' -DryRun
	Expect-Failure "--dry-run reports a failure too" $r 41 "target_not_found"

	$r = Expect-Unchanged "failure part way (41)" $project $before 41 "target_not_found" { Invoke-Ops $project @('{"op":"create-entity","name":"X"}', '{"op":"rename","target":"name:Origin","name":"Y"}', '{"op":"get-entity","target":"name:Nope"}', '{"op":"remove-entity","target":"name:Moved"}') }
	$results = @($r.Json.results)
	Check ($results.Count -eq 3 -and $results[0].ok -and $results[1].ok -and $results[2].ok -eq $false -and $r.Json.error.op_index -eq 2 -and $r.Json.op_count -eq 4 -and $r.Json.error.message -match "ops\[2\]") "failure part way -> results stop at the failed op, with its number and reason" $r.Stdout
	$r = Expect-Unchanged "failure part way (42)" $project $before 42 "transform_not_removable" { Invoke-Ops $project @('{"op":"rename","target":"name:Origin","name":"Y"}', '{"op":"remove-component","target":"name:Moved","type":"transform"}') }
	Check (-not (Test-TemporaryFiles $project)) "a failure part way leaves no temporary file"

	# 削除の共有を許さずに開いておくと、読めるが置き換えられないので保存できない(Serializer は読むときも読み書きで開く)
	$holder = [System.IO.FileStream]::new($project, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
	try
	{
		$r = Expect-Unchanged "save failure" $project $before 43 "save_failed" { Invoke-Op $project '{"op":"rename","target":"name:Origin","name":"Y"}' }
	}
	finally
	{
		$holder.Dispose()
	}
	Check (-not (Test-TemporaryFiles $project)) "a save failure leaves no temporary file"
	$r = Invoke-Op $project '{"op":"list-scenes"}'
	Check ($r.Code -eq 0) "the project still loads after the save failure" $r.Stdout

	# 一時ファイルの名前は実行ごとに分かれ、置き換えられるように同じフォルダにある
	$temporaryFile = $editor.GetType("DXForgeEditor.Automation.SceneOpProjectWriter", $true).GetMethod("TemporaryFile", $flags)
	$tempA = [string]$temporaryFile.Invoke($null, [object[]]@([string]$project))
	$tempB = [string]$temporaryFile.Invoke($null, [object[]]@([string]$project))
	Check ($tempA -ne $tempB -and (Split-Path $tempA) -eq (Split-Path $project) -and (Split-Path $tempB) -eq (Split-Path $project)) "each run has its own temporary file in the project's folder" "$tempA $tempB"

	# 並行で実行する。あとから保存したものが勝つが、失敗したものの変更は保存されず、一時ファイルも残らない
	$parallel = New-EmptyProject "Parallel"
	$runs = @(0..7 | ForEach-Object {
			$psi = [System.Diagnostics.ProcessStartInfo]::new($Cli)
			$psi.UseShellExecute = $false
			$psi.RedirectStandardOutput = $true
			$psi.RedirectStandardError = $true
			foreach ($a in @("--load-project", $parallel, "--op", "{`"op`":`"create-entity`",`"name`":`"Run$_`"}", "--json")) { $psi.ArgumentList.Add($a) }
			[pscustomobject]@{ Name = "Run$_"; Process = [System.Diagnostics.Process]::Start($psi) }
		})
	foreach ($run in $runs)
	{
		$stderrTask = $run.Process.StandardError.ReadToEndAsync()
		$null = $run.Process.StandardOutput.ReadToEnd()
		if (-not $run.Process.WaitForExit(120000)) { $run.Process.Kill($true) }
		$null = $stderrTask.Result
		$run | Add-Member Code $run.Process.ExitCode
		$run.Process.Dispose()
	}
	# NOTE: 読むときや置き換えるときに、ほかの実行と置き換えが読み込みと重なって失敗することはある(43)。読み込み同士は重ならない(#193)。失敗は許すが、その変更が残るのは許さない
	$unexpected = @($runs | Where-Object { $_.Code -notin 0, 43 })
	Write-Host "       parallel exit codes: $(($runs | ForEach-Object { $_.Code }) -join ' ')"
	Check ($unexpected.Count -eq 0) "parallel runs -> each one succeeds or fails cleanly" (($runs | ForEach-Object { "$($_.Name)=$($_.Code)" }) -join " ")
	$r = Invoke-Op $parallel '{"op":"list-entities"}'
	$savedNames = @($r.Json.results[0].entities | ForEach-Object { $_.name })
	$succeeded = @($runs | Where-Object { $_.Code -eq 0 } | ForEach-Object { $_.Name })
	Check ($r.Code -eq 0 -and $savedNames.Count -ge 1 -and @($savedNames | Where-Object { $_ -notin $succeeded }).Count -eq 0) "parallel runs -> only the changes of runs that succeeded are saved" "saved: $savedNames; succeeded: $succeeded"
	Check (-not (Test-TemporaryFiles $parallel)) "parallel runs leave no temporary file"

	# 読むだけの操作は並行で実行しても競合しない(#193: 読み込みは FileAccess.Read と FileShare.Read で開く)
	$readers = New-EmptyProject "ParallelRead"
	$runs = @(0..7 | ForEach-Object {
			$psi = [System.Diagnostics.ProcessStartInfo]::new($Cli)
			$psi.UseShellExecute = $false
			$psi.RedirectStandardOutput = $true
			$psi.RedirectStandardError = $true
			foreach ($a in @("--load-project", $readers, "--op", '{"op":"list-entities"}', "--json")) { $psi.ArgumentList.Add($a) }
			[pscustomobject]@{ Name = "Read$_"; Process = [System.Diagnostics.Process]::Start($psi) }
		})
	foreach ($run in $runs)
	{
		$stderrTask = $run.Process.StandardError.ReadToEndAsync()
		$null = $run.Process.StandardOutput.ReadToEnd()
		if (-not $run.Process.WaitForExit(120000)) { $run.Process.Kill($true) }
		$null = $stderrTask.Result
		$run | Add-Member Code $run.Process.ExitCode
		$run.Process.Dispose()
	}
	Write-Host "       parallel list-entities exit codes: $(($runs | ForEach-Object { $_.Code }) -join ' ')"
	Check (@($runs | Where-Object { $_.Code -ne 0 }).Count -eq 0) "parallel list-entities on one project -> all exit 0" (($runs | ForEach-Object { "$($_.Name)=$($_.Code)" }) -join " ")

	# 読み取り専用の .dxproj を読める
	$readOnly = New-EmptyProject "ReadOnlyProject"
	$beforeReadOnly = Read-Bytes $readOnly
	Set-ItemProperty -LiteralPath $readOnly -Name IsReadOnly -Value $true
	try
	{
		$r = Invoke-Op $readOnly '{"op":"list-entities"}'
		Check ($r.Code -eq 0) "read-only .dxproj -> list-entities exit 0" $r.Stdout
		Check (Bytes-Equal $beforeReadOnly (Read-Bytes $readOnly)) "read-only .dxproj is byte-identical after list-entities"
	}
	finally
	{
		Set-ItemProperty -LiteralPath $readOnly -Name IsReadOnly -Value $false
	}

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

	Write-Host "9. create-scene, remove-scene, set-active-scene"
	$project = New-EmptyProject "Scenes"
	$before = Read-Bytes $project
	$r = Invoke-Ops $project @('{"op":"create-scene","name":"Level2"}', '{"op":"create-entity","scene":"Level2","name":"InLevel2","as":"e"}', '{"op":"list-scenes"}') -DryRun
	Check ($r.Code -eq 0 -and $r.Json.changed -eq $true -and $r.Json.saved -eq $false -and (Bytes-Equal (Read-Bytes $project) $before)) "create-scene --dry-run -> changed, not saved, the .dxproj is unchanged" $r.Stdout
	$r = Invoke-Ops $project @('{"op":"create-scene","name":"Level2"}', '{"op":"create-entity","scene":"Level2","name":"InLevel2","as":"e"}', '{"op":"list-scenes"}')
	Expect-Saved "create-scene and create-entity in the same ops" $r
	$scenes = @(@($r.Json.results)[2].scenes)
	Check (@($r.Json.results)[0].scene.name -eq "Level2" -and @($r.Json.results)[0].scene.active -eq $false -and $scenes.Count -eq 2 -and $scenes[0].active -eq $true -and $scenes[1].entity_count -eq 1) "create-scene adds an inactive scene" $r.Stdout
	$loaded = (Load-Project $project).PSObject.BaseObject
	Check (@($loaded.Scenes).Count -eq 2 -and @($loaded.Scenes)[1].Name -eq "Level2" -and @(@($loaded.Scenes)[1].GameEntities).Count -eq 1 -and $loaded.ActiveScene.Name -eq "Sample Scene") "Project.Load reads the new scene and its entity"

	$before = Read-Bytes $project
	$null = Expect-Unchanged "create-scene with a used name" $project $before 42 "scene_exists" { Invoke-Op $project '{"op":"create-scene","name":"Level2"}' }
	$null = Expect-Unchanged "remove-scene of the active scene" $project $before 42 "scene_active" { Invoke-Op $project '{"op":"remove-scene","scene":"Sample Scene"}' }
	$null = Expect-Unchanged "remove-scene: not found" $project $before 41 "scene_not_found" { Invoke-Op $project '{"op":"remove-scene","scene":"Nope"}' }
	$null = Expect-Unchanged "set-active-scene: not found" $project $before 41 "scene_not_found" { Invoke-Op $project '{"op":"set-active-scene","scene":"Nope"}' }
	$null = Expect-Unchanged "a failure after create-scene saves nothing" $project $before 41 "scene_not_found" { Invoke-Ops $project @('{"op":"create-scene","name":"Level3"}', '{"op":"remove-scene","scene":"Nope"}') }
	$r = Invoke-Op $project '{"op":"set-active-scene","scene":"Sample Scene"}'
	Check ($r.Code -eq 0 -and $r.Json.changed -eq $false -and $r.Json.saved -eq $false -and (Bytes-Equal (Read-Bytes $project) $before)) "set-active-scene to the active scene -> changed false, saved false" $r.Stdout
	$r = Invoke-Ops $project @('{"op":"set-active-scene","scene":"Level2"}', '{"op":"remove-scene","scene":"Sample Scene"}') -DryRun
	Check ($r.Code -eq 0 -and $r.Json.changed -eq $true -and $r.Json.saved -eq $false -and (Bytes-Equal (Read-Bytes $project) $before)) "set-active-scene and remove-scene --dry-run -> not saved" $r.Stdout
	foreach ($case in @(
			@{ Name = "create-scene without name"; Op = '{"op":"create-scene"}'; Key = "name" },
			@{ Name = "create-scene with an empty name"; Op = '{"op":"create-scene","name":""}'; Key = "name" },
			@{ Name = "remove-scene without scene"; Op = '{"op":"remove-scene"}'; Key = "scene" },
			@{ Name = "set-active-scene without scene"; Op = '{"op":"set-active-scene"}'; Key = "scene" },
			@{ Name = "set-active-scene with an unknown key"; Op = '{"op":"set-active-scene","scene":"Level2","name":"x"}'; Key = "name" }))
	{
		$r = Expect-Unchanged $case.Name $project $before 40 "ops_invalid" { Invoke-Op $project $case.Op }
		Check ($r.Json.error.key -eq $case.Key) "$($case.Name) -> error.key $($case.Key)" $r.Stdout
	}

	# 新しくアクティブにしたシーンが game.bin になる(Sample Scene に 2 つ、Level2 に 1 つ)
	$r = Invoke-Ops $project @('{"op":"create-entity","name":"S1"}', '{"op":"create-entity","name":"S2"}')
	Expect-Saved "entities in the active scene" $r
	$binFile = Join-Path $work "scene_active.bin"
	$r = Invoke-Cli @("--load-project", $project, "--export-game-bin", $binFile) -Json
	Check ($r.Code -eq 0 -and $r.Json.entity_count -eq 2) "--export-game-bin uses the active scene before the change" $r.Stdout
	$r = Invoke-Ops $project @('{"op":"set-active-scene","scene":"Level2"}', '{"op":"remove-scene","scene":"Sample Scene"}')
	Expect-Saved "set-active-scene, then remove-scene" $r
	$loaded = (Load-Project $project).PSObject.BaseObject
	Check (@($loaded.Scenes).Count -eq 1 -and $loaded.ActiveScene.Name -eq "Level2") "Project.Load: the old scene is gone, Level2 is active" $loaded.ActiveScene.Name
	$r = Invoke-Cli @("--load-project", $project, "--export-game-bin", $binFile) -Json
	Check ($r.Code -eq 0 -and $r.Json.entity_count -eq 1) "--export-game-bin uses the new active scene" $r.Stdout
	$before = Read-Bytes $project
	$null = Expect-Unchanged "remove-scene of the last scene" $project $before 42 "last_scene" { Invoke-Op $project '{"op":"remove-scene","scene":"Level2"}' }

	# 消したシーンのエンティティを指す別名
	$project = New-EmptyProject "RemovedAlias"
	$before = Read-Bytes $project
	$r = Expect-Unchanged "an alias in a removed scene" $project $before 41 "target_not_found" { Invoke-Ops $project @('{"op":"create-scene","name":"Gone"}', '{"op":"create-entity","scene":"Gone","name":"E","as":"e"}', '{"op":"remove-scene","scene":"Gone"}', '{"op":"get-entity","target":"$e"}') }
	Check ($r.Json.error.op_index -eq 3) "an alias in a removed scene -> op_index 3" $r.Stdout

	# 同じ名前のシーンが 2 つ(エディタでは作れるので、.dxproj を直接書く)
	$project = New-EmptyProject "DuplicateScenes"
	$xml = [System.IO.File]::ReadAllText($project)
	$sceneBlock = [regex]::Match($xml, '(?s)<Scene z:Id="i2">.*?</Scene>').Value
	$xml = $xml.Replace($sceneBlock, $sceneBlock + $sceneBlock.Replace('z:Id="i2"', 'z:Id="i99"').Replace('<IsActive>true</IsActive>', '<IsActive>false</IsActive>'))
	[System.IO.File]::WriteAllText($project, $xml)
	$before = Read-Bytes $project
	$null = Expect-Unchanged "remove-scene with a duplicate name" $project $before 41 "ambiguous_target" { Invoke-Op $project '{"op":"remove-scene","scene":"Sample Scene"}' }

	Write-Host "10. import-asset with the fake importer"
	$FakeImporter = Join-Path $PSScriptRoot "fake_importer.cmd"
	$templates = Join-Path $work "templates"

	function New-Template([int]$type, [Guid]$guid)
	{
		$file = Join-Path $templates ("t" + (++$script:projectCounter) + ".asset")
		New-Asset $file $type $guid
		return $file
	}

	# 偽の importer への指示のファイル(import-asset の source)
	function New-Spec([hashtable]$spec)
	{
		$file = Join-Path $work ("spec" + (++$script:projectCounter) + ".json")
		[System.IO.File]::WriteAllText($file, ($spec | ConvertTo-Json -Depth 6))
		return $file
	}

	function Import-Json([string]$source, [string]$folder, $settings)
	{
		$op = @{ op = "import-asset"; source = $source }
		if ($folder) { $op.folder = $folder }
		if ($null -ne $settings) { $op.settings = $settings }
		return ($op | ConvertTo-Json -Depth 4 -Compress)
	}

	function Test-StagingLeft([string]$project) { return @(Get-ChildItem -Force -LiteralPath (Split-Path $project) -Filter ".dxforge-import-*").Count -gt 0 }

	function Get-ContentOf([string]$project) { return Join-Path (Split-Path $project) "Content" }

	# Content に何も増えず、一時フォルダが残らないことも確かめる
	function Expect-NothingImported([string]$name, [string]$project)
	{
		$content = Get-ContentOf $project
		Check (-not (Test-Path -LiteralPath $content)) "$name -> no Content folder" $content
		Check (-not (Test-StagingLeft $project)) "$name -> no staging folder"
	}

	function Read-Latin1([string]$file) { return [System.Text.Encoding]::Latin1.GetString([System.IO.File]::ReadAllBytes($file)) }

	$meshGuid = [Guid]::NewGuid()
	$mesh = New-Template 4 $meshGuid
	$okSpec = New-Spec @{ assets = @{ "model.asset" = $mesh }; warnings = @("from the importer") }

	# 成功: 同じ ops の後続が path: でも guid: でも指せて、保存され、game.bin にできる
	$project = New-EmptyProject "ImportOk"
	$content = Get-ContentOf $project
	$r = Invoke-Ops $project @(
		(Import-Json $okSpec),
		'{"op":"create-entity","name":"ByPath","as":"p"}',
		'{"op":"add-component","target":"$p","type":"geometry","asset":"default:cube"}',
		'{"op":"set-geometry","target":"$p","asset":"path:model.asset"}',
		'{"op":"create-entity","name":"ByGuid","as":"g"}',
		'{"op":"add-component","target":"$g","type":"geometry","asset":"default:cube"}',
		"{`"op`":`"set-geometry`",`"target`":`"`$g`",`"asset`":`"guid:$meshGuid`"}") -Importer $FakeImporter
	Expect-Saved "import-asset, then set-geometry by path: and guid:" $r
	$imported = @($r.Json.results)[0]
	Check (@($imported.assets).Count -eq 1 -and @($imported.assets)[0].path -eq "path:model.asset" -and @($imported.assets)[0].guid -eq "$meshGuid" -and @($imported.assets)[0].type -eq "mesh") "the result has the imported asset" $r.Stdout
	Check (@($imported.warnings) -contains "from the importer") "the result has the importer's warnings" $r.Stdout
	Check (@($r.Json.leftover_paths).Count -eq 0 -and $null -ne $r.Json.PSObject.Properties["leftover_paths"]) "success -> leftover_paths is an empty array" $r.Stdout
	Check ((Test-Path -LiteralPath (Join-Path $content "model.asset")) -and -not (Test-StagingLeft $project)) "the asset is in Content, no staging folder is left"
	Check ((Read-Latin1 (Join-Path $content "model.asset")).EndsWith($content)) "--sources-output is the final folder under Content, not the staging folder"
	$r = Invoke-Op $project '{"op":"get-entity","target":"name:ByPath"}'
	Check ((Get-Component $r.Json.results[0].entity "geometry").geometry -eq "$meshGuid") "set-geometry by path: is saved" $r.Stdout
	$r = Invoke-Op $project '{"op":"get-entity","target":"name:ByGuid"}'
	Check ((Get-Component $r.Json.results[0].entity "geometry").geometry -eq "$meshGuid") "set-geometry by guid: is saved" $r.Stdout
	$r = Invoke-Op $project "{`"op`":`"set-geometry`",`"target`":`"name:ByPath`",`"asset`":`"path:model.asset`"}"
	Check ($r.Code -eq 0 -and $r.Json.changed -eq $false) "the moved asset is found by path: in a later run" $r.Stdout
	$binFile = Join-Path $work "import_ok.bin"
	$r = Invoke-Cli @("--load-project", $project, "--export-game-bin", $binFile) -Json
	Check ($r.Code -eq 0 -and $r.Json.entity_count -eq 2) "--export-game-bin after importing" "$($r.Stdout) $($r.Stderr)"

	# 取り込みだけの ops も保存する。フォルダ(空白を含む)と複数のファイル
	$project = New-EmptyProject "ImportFolder"
	$content = Get-ContentOf $project
	$guidA = [Guid]::NewGuid()
	$guidB = [Guid]::NewGuid()
	$image = Join-Path $work "image.png"
	[System.IO.File]::WriteAllText($image, "not really an image")
	$spec = New-Spec @{ assets = @{ "a.asset" = (New-Template 4 $guidA); "b.asset" = (New-Template 6 $guidB); "a.fbm\t.png" = $image } }
	$r = Invoke-Ops $project @((Import-Json $spec "Models/New Sub")) -Importer $FakeImporter
	Expect-Saved "import-asset alone" $r
	$assets = @(@($r.Json.results)[0].assets)
	Check ($assets.Count -eq 2 -and $assets[0].path -eq "path:Models/New Sub/a.asset" -and $assets[1].guid -eq "$guidB" -and $assets[1].type -eq "texture") "several assets under folder" $r.Stdout
	$final = Join-Path $content "Models\New Sub"
	Check ((Test-Path -LiteralPath (Join-Path $final "a.asset")) -and (Test-Path -LiteralPath (Join-Path $final "b.asset")) -and (Test-Path -LiteralPath (Join-Path $final "a.fbm\t.png"))) "the files keep their places under folder, also the ones that are not assets"
	Check ((Read-Latin1 (Join-Path $final "a.asset")).EndsWith($final)) "--sources-output includes folder"

	# --dry-run
	$project = New-EmptyProject "ImportDry"
	$before = Read-Bytes $project
	$r = Invoke-Ops $project @((Import-Json $okSpec), '{"op":"create-entity","name":"E","as":"e"}', '{"op":"add-component","target":"$e","type":"geometry","asset":"path:model.asset"}') -DryRun -Importer $FakeImporter
	Check ($r.Code -eq 0 -and $r.Json.changed -eq $true -and $r.Json.saved -eq $false -and @(@($r.Json.results)[0].assets).Count -eq 1) "--dry-run reports the assets, changed, not saved" $r.Stdout
	Check (Bytes-Equal (Read-Bytes $project) $before) "--dry-run: the .dxproj is unchanged"
	Expect-NothingImported "--dry-run" $project

	# Content にあるアセットと同じ表記が、まだ Content にないアセットにも通る
	foreach ($spelling in @("./model.asset", ".//model.asset", "MODEL.asset")) {
		$r = Invoke-Ops $project @((Import-Json $okSpec), '{"op":"create-entity","name":"E","as":"e"}', ('{"op":"add-component","target":"$e","type":"geometry","asset":"path:' + $spelling + '"}')) -DryRun -Importer $FakeImporter
		Check ($r.Code -eq 0 -and $r.Json.changed -eq $true) "the imported asset is found by path:$spelling in the same ops" "$($r.Stdout) $($r.Stderr)"
	}
	# Content にあるアセットで見つからない表記は、まだ Content にないアセットでも見つからない(NUL で落ちない)
	foreach ($spelling in @("model.ASSET", "model.asset.", 'model\u0000.asset')) {
		$r = Invoke-Ops $project @((Import-Json $okSpec), '{"op":"create-entity","name":"E","as":"e"}', ('{"op":"add-component","target":"$e","type":"geometry","asset":"path:' + $spelling + '"}')) -DryRun -Importer $FakeImporter
		Check ($r.Code -eq 41 -and $r.Json.error.code -eq "asset_not_found") "path:$spelling is not found in the same ops, as in Content" "$($r.Code) $($r.Stdout) $($r.Stderr)"
	}
	Expect-NothingImported "--dry-run with other spellings of the path" $project

	# 後の操作の失敗
	$r = Expect-Unchanged "a failure after import-asset" $project $before 41 "target_not_found" { Invoke-Ops $project @((Import-Json $okSpec), '{"op":"get-entity","target":"name:Nope"}') -Importer $FakeImporter }
	Expect-NothingImported "a failure after import-asset" $project
	# 別の種類の失敗(42)でも同じ
	$r = Expect-Unchanged "a failed op (42) after import-asset" $project $before 42 "last_scene" { Invoke-Ops $project @((Import-Json $okSpec), '{"op":"remove-scene","scene":"Sample Scene"}') -Importer $FakeImporter }
	Expect-NothingImported "a failed op (42) after import-asset" $project

	# 途中までファイルを書いて失敗する
	$spec = New-Spec @{ assets = @{ "partial.asset" = $mesh }; errors = @("broken on purpose"); exit = 1 }
	$r = Expect-Unchanged "the importer writes a file, then exits 1" $project $before 42 "import_failed" { Invoke-Ops $project @((Import-Json $spec)) -Importer $FakeImporter }
	Check ($r.Json.error.message -match "broken on purpose" -and $r.Json.error.op_index -eq 0) "the importer's errors are in the message" $r.Stdout
	Expect-NothingImported "the importer writes a file, then exits 1" $project

	# 終了コードの対応(DXForgeImport の ExitCode)
	foreach ($case in @(
			@{ Exit = 3; Code = 41; Error = "source_not_found"; Key = "source" },
			@{ Exit = 4; Code = 42; Error = "import_unsupported_format"; Key = "source" },
			@{ Exit = 5; Code = 42; Error = "import_invalid_settings"; Key = "settings" },
			@{ Exit = 7; Code = 42; Error = "importer_unavailable"; Key = "source" },
			@{ Exit = 1; Code = 42; Error = "import_failed"; Key = "source" },
			@{ Exit = 10; Code = 42; Error = "import_failed"; Key = "source" },
			@{ Exit = 99; Code = 42; Error = "import_failed"; Key = "source"; NoJson = $true }))
	{
		$spec = New-Spec @{ exit = $case.Exit; no_json = [bool]$case.NoJson }
		$r = Expect-Unchanged "importer exit $($case.Exit)" $project $before $case.Code $case.Error { Invoke-Ops $project @((Import-Json $spec)) -Importer $FakeImporter }
		Check ($r.Json.error.key -eq $case.Key) "importer exit $($case.Exit) -> error.key $($case.Key)" $r.Stdout
		Expect-NothingImported "importer exit $($case.Exit)" $project
	}

	# source と importer
	$null = Expect-Unchanged "source does not exist" $project $before 41 "source_not_found" { Invoke-Ops $project @((Import-Json (Join-Path $work "missing.fbx"))) -Importer $FakeImporter }
	$null = Expect-Unchanged "the importer does not exist" $project $before 42 "importer_unavailable" { Invoke-Ops $project @((Import-Json $okSpec)) -Importer (Join-Path $work "missing.exe") }
	Expect-NothingImported "the importer does not exist" $project
	foreach ($case in @(
			@{ Name = "relative source"; Op = '{"op":"import-asset","source":"model.fbx"}'; Key = "source" },
			@{ Name = "no source"; Op = '{"op":"import-asset"}'; Key = "source" },
			@{ Name = "folder with .."; Op = (Import-Json $okSpec "a/../../b"); Key = "folder" },
			@{ Name = "absolute folder"; Op = (Import-Json $okSpec "C:\x"); Key = "folder" },
			@{ Name = "rooted folder"; Op = (Import-Json $okSpec "/x"); Key = "folder" },
			@{ Name = "settings is an array"; Op = (Import-Json $okSpec $null @(1)); Key = "settings" },
			@{ Name = "a setting is an object"; Op = (Import-Json $okSpec $null @{ A = @{ B = 1 } }); Key = "settings.A" }))
	{
		$r = Expect-Unchanged $case.Name $project $before 40 "ops_invalid" { Invoke-Ops $project @($case.Op) -Importer $FakeImporter }
		Check ($r.Json.error.key -eq $case.Key) "$($case.Name) -> error.key $($case.Key)" $r.Stdout
	}
	$r = Invoke-Cli @("--load-project", $project, "--frames", "1", "--importer", $FakeImporter) -Json
	Check ($r.Code -eq 2 -and $r.Json.error.code -eq "invalid_arguments") "--importer without ops -> exit 2" $r.Stdout

	# settings は --set で渡す
	$argsLog = Join-Path $work "args.log"
	$spec = New-Spec @{ assets = @{ "set.asset" = $mesh }; args_log = $argsLog }
	$r = Invoke-Ops $project @((Import-Json $spec $null ([ordered]@{ MipLevels = 3; Compress = $false; Name = "a b" }))) -DryRun -Importer $FakeImporter
	$logged = @(Get-Content -LiteralPath $argsLog)
	$sets = @(for ($i = 0; $i -lt $logged.Count; ++$i) { if ($logged[$i] -eq "--set") { $logged[$i + 1] } })
	Check ($r.Code -eq 0 -and ($sets -join "|") -eq "MipLevels=3|Compress=false|Name=a b") "settings -> --set Name=Value" ($logged -join " ")
	Check (-not (($logged -join "`n") -match "--sources-output\n[^\n]*\.dxforge-import-") -and $logged -contains "--json" -and (($logged -join "`n") -match "--output\n[^\n]*\.dxforge-import-")) "--output is the staging folder, --sources-output is not" ($logged -join " ")

	# 出力がない
	$spec = New-Spec @{ }
	$r = Expect-Unchanged "the importer exits 0 without an asset" $project $before 42 "import_failed" { Invoke-Ops $project @((Import-Json $spec)) -Importer $FakeImporter }
	Expect-NothingImported "the importer exits 0 without an asset" $project

	# 衝突
	$project = New-EmptyProject "ImportCollision"
	$content = Get-ContentOf $project
	$existing = Join-Path $content "model.asset"
	New-Asset $existing 4 ([Guid]::NewGuid())
	$before = Read-Bytes $project
	$existingBytes = Read-Bytes $existing
	$r = Expect-Unchanged "a file that already exists" $project $before 42 "asset_exists" { Invoke-Ops $project @((Import-Json $okSpec)) -Importer $FakeImporter }
	Check ((Bytes-Equal (Read-Bytes $existing) $existingBytes) -and -not (Test-StagingLeft $project)) "a file that already exists -> its bytes stay, no staging folder"
	$project = New-EmptyProject "ImportTwice"
	$before = Read-Bytes $project
	$r = Expect-Unchanged "the same file twice in the ops" $project $before 42 "asset_exists" { Invoke-Ops $project @((Import-Json $okSpec), (Import-Json $okSpec)) -Importer $FakeImporter }
	Check ($r.Json.error.op_index -eq 1) "the same file twice -> op_index 1" $r.Stdout
	Expect-NothingImported "the same file twice in the ops" $project

	# エディタが開いている(取り込みも断る。インポーターは起動しない)
	$project = New-EmptyProject "ImportLocked"
	$before = Read-Bytes $project
	$lockLog = Join-Path $work "lock_args.log"
	$spec = New-Spec @{ assets = @{ "locked.asset" = $mesh }; args_log = $lockLog }
	Set-EditorProject (Load-Project $project)
	try
	{
		$null = Expect-Unchanged "import-asset while the editor is open" $project $before 44 "editor_open" { Invoke-Ops $project @((Import-Json $spec)) -Importer $FakeImporter }
		$null = Expect-Unchanged "import-asset --dry-run while the editor is open" $project $before 44 "editor_open" { Invoke-Ops $project @((Import-Json $spec)) -DryRun -Importer $FakeImporter }
	}
	finally
	{
		Set-EditorProject $null
	}
	Check (-not (Test-Path -LiteralPath $lockLog)) "the importer did not run while the editor was open"
	Expect-NothingImported "import-asset while the editor is open" $project

	Write-Host "11. import-asset: failures while committing"
	# Content への移動の途中で失敗する: 後の取り込みの指示で、前の取り込みの移動先をフォルダとして塞ぐ
	$project = New-EmptyProject "CommitFailure"
	$content = Get-ContentOf $project
	$before = Read-Bytes $project
	$blocked = Join-Path $content "z.asset"
	$specA = New-Spec @{ assets = @{ "a.asset" = $mesh; "z.asset" = (New-Template 4 ([Guid]::NewGuid())) } }
	$specB = New-Spec @{ assets = @{ "b.asset" = (New-Template 4 ([Guid]::NewGuid())) }; mkdir = @($blocked) }
	$r = Expect-Unchanged "a move into Content fails part way" $project $before 43 "asset_commit_failed" { Invoke-Ops $project @((Import-Json $specA), (Import-Json $specB)) -Importer $FakeImporter }
	Check (-not (Test-Path -LiteralPath (Join-Path $content "a.asset")) -and -not (Test-Path -LiteralPath (Join-Path $content "b.asset"))) "a move fails part way -> the moved file is removed" (Get-ChildItem -Recurse -Force $content | Out-String)
	Check ((Test-Path -LiteralPath $blocked -PathType Container) -and @(Get-ChildItem -Force -LiteralPath $content).Count -eq 1) "a move fails part way -> only the folder that was already there stays"
	Check (@($r.Json.leftover_paths).Count -eq 0 -and -not (Test-StagingLeft $project)) "a move fails part way -> no leftovers, no staging folder" $r.Stdout

	# 保存の失敗: 移したファイルと、そのために作ったフォルダ(Content を含む)を消す
	$project = New-EmptyProject "SaveFailure"
	$before = Read-Bytes $project
	Set-ItemProperty -LiteralPath $project -Name IsReadOnly -Value $true
	try
	{
		$r = Expect-Unchanged "saving fails after the move" $project $before 43 "save_failed" { Invoke-Ops $project @((Import-Json $okSpec "new/sub")) -Importer $FakeImporter }
	}
	finally
	{
		Set-ItemProperty -LiteralPath $project -Name IsReadOnly -Value $false
	}
	Expect-NothingImported "saving fails after the move" $project
	Check (@($r.Json.leftover_paths).Count -eq 0) "saving fails -> no leftovers" $r.Stdout

	# 保存の失敗でも、もとからあった Content のファイルには触らない
	$project = New-EmptyProject "SaveFailureKeeps"
	$content = Get-ContentOf $project
	$kept = Join-Path $content "kept.asset"
	New-Asset $kept 4 ([Guid]::NewGuid())
	$keptBytes = Read-Bytes $kept
	Set-ItemProperty -LiteralPath $project -Name IsReadOnly -Value $true
	try
	{
		$r = Invoke-Ops $project @((Import-Json $okSpec)) -Importer $FakeImporter
	}
	finally
	{
		Set-ItemProperty -LiteralPath $project -Name IsReadOnly -Value $false
	}
	Check ($r.Code -eq 43 -and (Bytes-Equal (Read-Bytes $kept) $keptBytes) -and @(Get-ChildItem -Force -LiteralPath $content).Count -eq 1) "saving fails -> the files that were in Content stay" $r.Stdout

	# ここからはプロセスの外から起こせないので、SceneOpAssetStaging を直接呼ぶ
	$stagingType = $editor.GetType("DXForgeEditor.Automation.SceneOpAssetStaging", $true)
	$documentType = $editor.GetType("DXForgeEditor.Automation.SceneOpDocument", $true)
	function New-Staging([string]$project, [string]$importer) { return $stagingType.GetConstructors($flags)[0].Invoke([object[]]@($project, $importer)) }
	function Invoke-StagingImport($staging, [string]$opJson)
	{
		$op = @($documentType.GetMethod("ParseSingle", $flags).Invoke($null, [object[]]@($opJson)))[0]
		$null = $stagingType.GetMethod("Import", $flags).Invoke($staging, [object[]]@($op, [System.Text.Json.Nodes.JsonObject]::new(), [System.Text.Json.Nodes.JsonArray]::new()))
	}

	# 戻す処理そのものの失敗: 消せないものは LeftoverPaths に入る
	$project = New-EmptyProject "RollbackFailure"
	$content = Get-ContentOf $project
	$staging = New-Staging $project $FakeImporter
	$spec = New-Spec @{ assets = @{ "held.asset" = $mesh; "free.asset" = (New-Template 4 ([Guid]::NewGuid())) } }
	Invoke-StagingImport $staging (Import-Json $spec "deep/er")
	$null = $stagingType.GetMethod("MoveToContent", $flags).Invoke($staging, @())
	$held = Join-Path $content "deep\er\held.asset"
	$free = Join-Path $content "deep\er\free.asset"
	$holder = [System.IO.FileStream]::new($held, [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
	try
	{
		$null = $stagingType.GetMethod("RollbackMoved", $flags).Invoke($staging, @())
		$staging.Dispose()
		$left = @($stagingType.GetProperty("LeftoverPaths", $flags).GetValue($staging))
		Check ($left -contains $held) "rollback cannot delete a file -> LeftoverPaths has it" ($left -join "; ")
		Check ($left -contains (Join-Path $content "deep\er")) "rollback cannot delete a folder that still has a file -> LeftoverPaths has it" ($left -join "; ")
		Check (-not (Test-Path -LiteralPath $free) -and (Test-Path -LiteralPath $held)) "rollback removes what it can"
		Check (-not (Test-StagingLeft $project)) "rollback failure -> the staging folder is removed"
	}
	finally
	{
		$holder.Dispose()
	}

	# タイムアウト
	$project = New-EmptyProject "Timeout"
	$staging = New-Staging $project $FakeImporter
	$stagingType.GetProperty("ImporterTimeout", $flags).SetValue($staging, [TimeSpan]::FromSeconds(1))
	$spec = New-Spec @{ sleep = 60; assets = @{ "late.asset" = $mesh } }
	$elapsed = [System.Diagnostics.Stopwatch]::StartNew()
	$errorCode = $null
	try { Invoke-StagingImport $staging (Import-Json $spec) }
	catch
	{
		$inner = $_.Exception
		while ($inner.InnerException) { $inner = $inner.InnerException }
		if ($inner.GetType().Name -eq "SceneOpException") { $errorCode = $inner.GetType().GetProperty("Code", $flags).GetValue($inner) } else { $errorCode = "$inner" }
	}
	$elapsed.Stop()
	Check ($errorCode -eq "import_failed") "the importer does not finish in time -> import_failed" $errorCode
	Check ($elapsed.Elapsed.TotalSeconds -lt 30) "the importer does not finish in time -> it is killed (not waited for)" $elapsed.Elapsed
	$staging.Dispose()
	Check (-not (Test-StagingLeft $project) -and -not (Test-Path -LiteralPath (Get-ContentOf $project))) "the importer does not finish in time -> nothing is left"

	Write-Host "12. import-asset with the real DXForgeImport"
	$checker = Join-Path $RepoRoot "DXForgeEditor\Resources\TextureEditor\Checker64.png"
	# --importer なしの既定の場所(CI でも ContentTools.dll の前に断るケース)
	$project = New-EmptyProject "RealDefault"
	$before = Read-Bytes $project
	$unsupported = Join-Path $work "unsupported.txt"
	[System.IO.File]::WriteAllText($unsupported, "x")
	$null = Expect-Unchanged "default importer: unsupported format" $project $before 42 "import_unsupported_format" { Invoke-Ops $project @((Import-Json $unsupported)) }
	$null = Expect-Unchanged "default importer: unknown setting" $project $before 42 "import_invalid_settings" { Invoke-Ops $project @((Import-Json $checker $null @{ NoSuchSetting = 1 })) }
	Expect-NothingImported "default importer: failures" $project

	$realReady = (Test-Path (Join-Path $OutDir "DXForgeImport.exe")) -and (Test-Path (Join-Path $OutDir "ContentTools.dll")) -and (Test-Path -LiteralPath $FbxFile)
	if (-not $realReady)
	{
		if ($RequireRealImporter) { Check $false "the real importer is available (DXForgeImport.exe, ContentTools.dll, $FbxFile)" }
		else
		{
			Write-Host "  SKIP real importer (no ContentTools.dll in $OutDir or no $FbxFile; -RequireRealImporter makes this a failure)"
			$skips.Add("real importer")
		}
	}
	else
	{
		# .asset の生のバイトに、一時フォルダのパスが UTF-8 でも UTF-16 でも残っていない(すべてのファイル)
		function Test-NoStagingPath([string]$name, [string]$folder)
		{
			$utf8Marker = ".dxforge-import-"
			$utf16Marker = [System.Text.Encoding]::Latin1.GetString([System.Text.Encoding]::Unicode.GetBytes($utf8Marker))
			$files = @(Get-ChildItem -LiteralPath $folder -Recurse -File -Filter "*.asset")
			Check ($files.Count -gt 0) "$name -> there are .asset files to check"
			foreach ($file in $files)
			{
				$raw = Read-Latin1 $file.FullName
				Check (-not $raw.Contains($utf8Marker) -and -not $raw.Contains($utf16Marker)) "$name -> no staging path in the bytes of $($file.Name)"
			}
		}

		$project = New-EmptyProject "RealBox"
		$content = Get-ContentOf $project
		$r = Invoke-Ops $project @(
			(Import-Json $FbxFile "Models"),
			'{"op":"create-entity","name":"Box","as":"b"}',
			'{"op":"add-component","target":"$b","type":"geometry","asset":"default:cube"}',
			'{"op":"set-geometry","target":"$b","asset":"path:Models/box.asset"}')
		Expect-Saved "real importer: box.fbx without --importer, then set-geometry" $r
		Write-Host "       assets: $(@(@($r.Json.results)[0].assets) | ConvertTo-Json -Compress)"
		$boxGuid = @(@(@($r.Json.results)[0].assets) | Where-Object { $_.path -eq "path:Models/box.asset" })[0].guid
		Check ($null -ne $boxGuid -and (Get-Component @($r.Json.results)[3].entity "geometry").geometry -eq $boxGuid -and -not (Test-StagingLeft $project)) "real importer: box.fbx -> the entity has the imported GUID, no staging folder" $r.Stdout
		Test-NoStagingPath "real importer: box.fbx" $content
		$binFile = Join-Path $work "real_box.bin"
		$r = Invoke-Cli @("--load-project", $project, "--export-game-bin", $binFile) -Json
		Write-Host "       --export-game-bin: exit $($r.Code), entity_count $($r.Json.entity_count), $((Get-Item -LiteralPath $binFile).Length) bytes"
		Check ($r.Code -eq 0 -and $r.Json.entity_count -eq 1) "real importer: box.fbx -> --export-game-bin" "$($r.Stdout) $($r.Stderr)"
		$loaded = (Load-Project $project).PSObject.BaseObject
		$loadedGeometry = @(@($loaded.ActiveScene.GameEntities)[0].Components | Where-Object { $_.GetType().Name -eq "Geometry" })[0]
		Check ("$($loadedGeometry.GeometryGuid)" -eq $boxGuid) "real importer: Project.Load sees the imported GUID"

		# 埋め込みテクスチャ(作り方は import_cli_test.ps1 と同じ)
		$lines = [System.Collections.Generic.List[string]]([System.IO.File]::ReadAllLines($FbxFile))
		if ($lines[0] -notmatch '^; FBX 7') { Check $false "$FbxFile is an ASCII FBX 7 file" }
		else
		{
			$base64 = [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($checker))
			$objectsEnd = $lines.IndexOf("Connections:  {") - 1
			while ($lines[$objectsEnd] -ne "}") { --$objectsEnd }
			$lines.InsertRange($objectsEnd, [string[]]@(
				'    Video: 900000001, "Video::embedded_checker", "Clip" {',
				'        Type: "Clip"',
				'        Properties70:  {',
				'            P: "Path", "KString", "XRefUrl", "", "embedded_checker.png"',
				'        }',
				'        Filename: "embedded_checker.png"',
				'        RelativeFilename: "embedded_checker.png"',
				('        Content: , "' + $base64 + '"'),
				'    }'))
			$count = $lines.IndexOf("Definitions:  {") + 1
			while ($lines[$count] -notmatch '^\s+Count: (\d+)$') { ++$count }
			$lines[$count] = "    Count: $([int]$Matches[1] + 1)"
			$lines.InsertRange($count + 1, [string[]]@('    ObjectType: "Video" {', '        Count: 1', '    }'))
			$embeddedFbx = Join-Path $work "embedded_box.fbx"
			[System.IO.File]::WriteAllLines($embeddedFbx, $lines)

			$project = New-EmptyProject "RealEmbedded"
			$content = Get-ContentOf $project
			$r = Invoke-Ops $project @(
				(Import-Json $embeddedFbx "Models"),
				'{"op":"create-entity","name":"Box","as":"b"}',
				'{"op":"add-component","target":"$b","type":"geometry","asset":"path:Models/embedded_box.asset"}')
			Expect-Saved "real importer: FBX with an embedded texture" $r
			Write-Host "       assets: $(@(@($r.Json.results)[0].assets) | ConvertTo-Json -Compress)"
			$textureAsset = Join-Path $content "Models\embedded_checker.asset"
			$finalImage = Join-Path $content "Models\embedded_box.fbm\embedded_checker.png"
			Check ((Test-Path -LiteralPath $textureAsset) -and (Test-Path -LiteralPath $finalImage)) "real importer: the embedded texture and its image are in Content"
			Check (@(@(@($r.Json.results)[0].assets) | Where-Object { $_.type -eq "texture" }).Count -eq 1) "real importer: the result lists the embedded texture" $r.Stdout
			Check ((Read-Latin1 $textureAsset).Contains($finalImage)) "real importer: the texture's Sources point at the image under Content" $finalImage
			Test-NoStagingPath "real importer: embedded texture" $content
			Check (-not (Test-StagingLeft $project)) "real importer: embedded texture -> no staging folder"
			$binFile = Join-Path $work "real_embedded.bin"
			$r = Invoke-Cli @("--load-project", $project, "--export-game-bin", $binFile) -Json
			Write-Host "       --export-game-bin: exit $($r.Code), entity_count $($r.Json.entity_count), $((Get-Item -LiteralPath $binFile).Length) bytes"
			Check ($r.Code -eq 0 -and $r.Json.entity_count -eq 1) "real importer: embedded texture -> --export-game-bin" "$($r.Stdout) $($r.Stderr)"
		}

		# 画像を直接取り込み、settings を実物に渡す
		$project = New-EmptyProject "RealTexture"
		$content = Get-ContentOf $project
		$r = Invoke-Ops $project @((Import-Json $checker "Textures" @{ Compress = $false; MipLevels = 1 }))
		Expect-Saved "real importer: an image with settings" $r
		Check ((Read-Latin1 (Join-Path $content "Textures\checker64.asset")).Contains($checker)) "real importer: an image outside the output keeps its own path in Sources"
		Test-NoStagingPath "real importer: an image" $content
		$before = Read-Bytes $project
		$existingBytes = Read-Bytes (Join-Path $content "Textures\checker64.asset")
		$null = Expect-Unchanged "real importer: importing the same image again" $project $before 42 "asset_exists" { Invoke-Ops $project @((Import-Json $checker "Textures")) }
		Check ((Bytes-Equal (Read-Bytes (Join-Path $content "Textures\checker64.asset")) $existingBytes)) "real importer: the existing asset keeps its bytes"

		# LOD のある FBX
		if ($LodFbxFile -and (Test-Path -LiteralPath $LodFbxFile))
		{
			$project = New-EmptyProject "RealLod"
			$content = Get-ContentOf $project
			$r = Invoke-Ops $project @((Import-Json $LodFbxFile))
			Expect-Saved "real importer: an FBX with LODs" $r
			$assets = @(@($r.Json.results)[0].assets)
			Write-Host "       assets: $($assets | ConvertTo-Json -Compress)"
			Check ($assets.Count -ge 1 -and @($assets | Where-Object { $_.type -ne "mesh" }).Count -eq 0) "real importer: an FBX with LODs -> mesh assets" $r.Stdout
			Test-NoStagingPath "real importer: an FBX with LODs" $content
		}
		else { Write-Host "  (-LodFbxFile is not given: an FBX with LODs is not checked)" }

		# LOD グループが 2 つ以上ある FBX: .asset が複数でき、全部が assets に出て Content に移る
		if (-not $MultiLodFbxFile)
		{
			$MultiLodFbxFile = Join-Path $work "multilod.fbx"
			python (Join-Path $RepoRoot "DXForgeImport\Tests\make_multilod_fbx.py") $FbxFile $MultiLodFbxFile
			Check ($LASTEXITCODE -eq 0) "real importer: make_multilod_fbx.py makes an FBX from $FbxFile"
		}
		$project = New-EmptyProject "RealMultiLod"
		$content = Get-ContentOf $project
		$r = Invoke-Ops $project @((Import-Json $MultiLodFbxFile "Models"))
		Expect-Saved "real importer: an FBX with several LOD groups" $r
		$assets = @(@($r.Json.results)[0].assets)
		Write-Host "       assets: $($assets | ConvertTo-Json -Compress)"
		$files = @(Get-ChildItem -LiteralPath (Join-Path $content "Models") -Filter "*.asset" -File -ErrorAction SilentlyContinue)
		Check ($assets.Count -ge 2 -and $files.Count -eq $assets.Count) "real importer: several LOD groups -> several assets, as many files in Content" $r.Stdout
		foreach ($asset in $assets)
		{
			Check (Test-Path -LiteralPath (Join-Path $content ($asset.path -replace '^path:', ''))) "real importer: several LOD groups -> $($asset.path) is in Content"
		}
		Check (-not (Test-StagingLeft $project)) "real importer: several LOD groups -> no staging folder"
		Test-NoStagingPath "real importer: several LOD groups" $content
	}

	Write-Host "13. staging folders left by a killed process"
	function New-LeftStaging([string]$project, [switch]$NoLock)
	{
		$folder = Join-Path (Split-Path $project) (".dxforge-import-" + [Guid]::NewGuid().ToString("N"))
		$null = New-Item -ItemType Directory -Force (Join-Path $folder "0\Models")
		[System.IO.File]::WriteAllText((Join-Path $folder "0\Models\left.asset"), "left")
		if (-not $NoLock) { [System.IO.File]::WriteAllText((Join-Path $folder "staging.lock"), "") }
		return $folder
	}
	$changeOp = '{"op":"create-entity","name":"AfterKill"}'

	# 所有者がいない(ロックが空いている) -> 消す。1 回の実行で一度だけ。JSON はそのまま
	$project = New-EmptyProject "SweepAbandoned"
	$folder = New-LeftStaging $project
	$second = New-LeftStaging $project
	$r = Invoke-Ops $project @($changeOp)
	Expect-Saved "abandoned staging folders" $r
	Check (-not (Test-Path -LiteralPath $folder) -and -not (Test-Path -LiteralPath $second)) "abandoned staging folders -> removed" $r.Stderr
	Check (@($r.Json.leftover_paths).Count -eq 0) "abandoned staging folders -> leftover_paths is empty" $r.Stdout
	Check (([regex]::Matches($r.Stderr, [regex]::Escape($folder))).Count -eq 1) "abandoned staging folders -> swept once in a run" $r.Stderr

	# --dry-run と読むだけの操作では消さない
	$project = New-EmptyProject "SweepDryRun"
	$folder = New-LeftStaging $project
	$r = Invoke-Ops $project @($changeOp) -DryRun
	Check ($r.Code -eq 0 -and (Test-Path -LiteralPath $folder)) "--dry-run -> a left staging folder stays" $r.Stdout
	$r = Invoke-Ops $project @('{"op":"list-entities"}')
	Check ($r.Code -eq 0 -and (Test-Path -LiteralPath $folder)) "read-only ops -> a left staging folder stays" $r.Stdout

	# 操作が失敗しても片付けるが、.dxproj は変わらない
	$before = Read-Bytes $project
	$r = Expect-Unchanged "a failing op with a left staging folder" $project $before 41 "target_not_found" { Invoke-Ops $project @('{"op":"remove-entity","target":"guid:00000000-0000-4000-8000-00000000dead"}') }
	Check (-not (Test-Path -LiteralPath $folder)) "a failing op -> the left staging folder is removed" $r.Stderr

	# 使用中(別のプロセスがロックを持つ) -> 消さず、警告もしない
	$project = New-EmptyProject "SweepInUse"
	$folder = New-LeftStaging $project
	$holder = [System.IO.FileStream]::new((Join-Path $folder "staging.lock"), [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None)
	try
	{
		$r = Invoke-Ops $project @($changeOp)
		Expect-Saved "a staging folder in use" $r
		Check ((Test-Path -LiteralPath (Join-Path $folder "0\Models\left.asset")) -and $r.Stderr -notmatch [regex]::Escape($folder)) "a staging folder in use -> not touched, no warning" $r.Stderr
	}
	finally { $holder.Dispose() }

	# ロックのないフォルダ(この変更より前の残り、作成の瞬間) -> 消さずに stderr に警告。stdout の JSON は読める
	$project = New-EmptyProject "SweepNoLock"
	$folder = New-LeftStaging $project -NoLock
	$r = Invoke-Ops $project @($changeOp)
	Expect-Saved "a staging folder without a lock" $r
	Check ((Test-Path -LiteralPath (Join-Path $folder "0\Models\left.asset")) -and $r.Stderr -match [regex]::Escape($folder)) "a staging folder without a lock -> kept, warned on stderr" $r.Stderr
	Check (@($r.Json.leftover_paths).Count -eq 0) "a staging folder without a lock -> not in leftover_paths" $r.Stdout

	# 名前が自分の形でないものには触らない
	$project = New-EmptyProject "SweepOtherName"
	$other = Join-Path (Split-Path $project) ".dxforge-import-keep"
	$null = New-Item -ItemType Directory $other
	[System.IO.File]::WriteAllText((Join-Path $other "staging.lock"), "")
	$r = Invoke-Ops $project @($changeOp)
	Check ($r.Code -eq 0 -and (Test-Path -LiteralPath $other) -and $r.Stderr -notmatch "keep") "a folder not named by dxforge -> not touched, no warning" $r.Stderr

	# 中のファイルを消せない -> leftover_paths に出して、操作は成功。次の実行で消える
	$project = New-EmptyProject "SweepUndeletable"
	$folder = New-LeftStaging $project
	$holder = [System.IO.FileStream]::new((Join-Path $folder "0\Models\left.asset"), [System.IO.FileMode]::Open, [System.IO.FileAccess]::Read, [System.IO.FileShare]::Read)
	try
	{
		$r = Invoke-Ops $project @($changeOp)
		Expect-Saved "a left staging folder that cannot be deleted" $r
		Check (@($r.Json.leftover_paths) -contains $folder) "a left staging folder that cannot be deleted -> leftover_paths has it" $r.Stdout
	}
	finally { $holder.Dispose() }
	$r = Invoke-Ops $project @('{"op":"create-entity","name":"AfterRelease"}')
	Check ($r.Code -eq 0 -and -not (Test-Path -LiteralPath $folder)) "the next run removes it" "$($r.Stdout) $($r.Stderr)"

	# dxforge と実際に Process.Start された importer の組み合わせ: dxforge だけを kill しても、importer が生きている間はロックが続く
	$project = New-EmptyProject "SweepKilled"
	$argsLog = Join-Path $work "killed_args.txt"
	$spec = New-Spec @{ args_log = $argsLog; sleep = 12; assets = @{ "killed.asset" = $mesh } }
	$psi = [System.Diagnostics.ProcessStartInfo]::new($Cli)
	$psi.UseShellExecute = $false
	$psi.RedirectStandardOutput = $true
	$psi.RedirectStandardError = $true
	foreach ($a in @("--load-project", $project, "--op", (Import-Json $spec), "--importer", $FakeImporter, "--json")) { $psi.ArgumentList.Add($a) }
	$killed = [System.Diagnostics.Process]::Start($psi)
	$deadline = [DateTime]::UtcNow.AddSeconds(30)
	while (-not (Test-Path -LiteralPath $argsLog) -and [DateTime]::UtcNow -lt $deadline) { Start-Sleep -Milliseconds 100 }
	Check (Test-Path -LiteralPath $argsLog) "killed dxforge: the importer started"
	$killed.Kill() # 子のプロセスは残す
	$killed.WaitForExit()
	$left = @(Get-ChildItem -Force -LiteralPath (Split-Path $project) -Directory -Filter ".dxforge-import-*")
	Check ($left.Count -eq 1 -and -not (Test-Path -LiteralPath (Get-ContentOf $project))) "killed dxforge: a staging folder is left, nothing in Content"
	$r = Invoke-Ops $project @($changeOp)
	Expect-Saved "killed dxforge, the importer still runs" $r
	Check ($left.Count -eq 1 -and (Test-Path -LiteralPath $left[0].FullName) -and $r.Stderr -notmatch "\.dxforge-import-") "killed dxforge: the importer still holds the inherited lock -> the folder is not touched" $r.Stderr
	$lockFree = $false
	$deadline = [DateTime]::UtcNow.AddSeconds(60)
	while (-not $lockFree -and [DateTime]::UtcNow -lt $deadline)
	{
		try { [System.IO.FileStream]::new((Join-Path $left[0].FullName "staging.lock"), [System.IO.FileMode]::Open, [System.IO.FileAccess]::ReadWrite, [System.IO.FileShare]::None).Dispose(); $lockFree = $true }
		catch { Start-Sleep -Milliseconds 200 }
	}
	Check $lockFree "killed dxforge: the lock is free once the importer ends"
	$r = Invoke-Ops $project @('{"op":"create-entity","name":"AfterImporter"}')
	Check ($r.Code -eq 0 -and -not (Test-StagingLeft $project)) "killed dxforge: the next run removes the folder after the importer ended" "$($r.Stdout) $($r.Stderr)"

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
