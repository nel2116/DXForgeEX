<#
.SYNOPSIS
    コマンドラインの dxforge(--load-project、Issue #11)の回帰テスト。

.DESCRIPTION
    x64\<Configuration>\dxforge.exe を別のプロセスとして実行し、終了コードと標準出力のJSONを確かめる。
    エディタのウィンドウもGPUも使わない。

    確かめること:
      1. 引数の誤り(終了コード2、error.code = invalid_arguments)と --help。
      2. .dxproj の異常: ない(30)、XMLが壊れている・プロジェクトではない・シーンの一覧がない(31)、
         アクティブなシーンがない・--scene の名前がない(32)。DebugEditor でも Debug.Assert で落ちない。
      3. --export-game-bin のバイト列が、エディタの Project.SaveToBinary(リフレクションで呼ぶ)が同じ .dxproj から書くものと、
         テストが game.bin の形式どおりに組んだものの両方と一致する(スクリプトのあるシーンも)。書き出せない場所は 36。
      4. 実行(ランナー EngineUnitTests.exe が必要): 終了コード0、エンティティの数・名前・Transform(回転はクォータニオン)が
         期待どおり。--scene、--dt、--frames 0、--keep-game-bin、日本語のパスと名前、JSONなしの出力、一時フォルダの後始末。
      5. スクリプトのあるシーン(33)、ランナーがない(34)、ランナーを起動できない(35)。
      6. Geometry コンポーネント(Primal Day 079, #32)のあるシーン: 読み込める(保存したジオメトリとマテリアルを読み、
         保存し直しても変わらない)。game.bin には Geometry を書かない(同じエンティティで Geometry のない TransformOnly と
         1 バイトも違わない)ので書き出しも実行も終了コード 0 で、標準エラーに飛ばした警告が出る。JSON の形は変わらない。
    どの場合も、標準出力は(--json なら)1つのJSONだけで、ConvertFrom-Json で読める。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。x64\<Configuration>\ の dxforge.exe と DXForgeEditor.dll を使う。

.PARAMETER Runner
    ヘッドレスのランナー。既定は dxforge と同じく x64\<Debug|Release>\EngineUnitTests.exe。

.PARAMETER RequireRunner
    ランナーがないときに、実行のケースを SKIP にせず失敗にする(CI はこれを付ける)。

.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeCli/Tests/load_project_cli_test.ps1 -RequireRunner
#>
[CmdletBinding()]
param(
	[ValidateSet("DebugEditor", "ReleaseEditor")]
	[string]$Configuration = "DebugEditor",
	[string]$Runner,
	[switch]$RequireRunner
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$OutDir = Join-Path $RepoRoot "x64\$Configuration"
$Cli = Join-Path $OutDir "dxforge.exe"
$NativeConfiguration = if ($Configuration -eq "DebugEditor") { "Debug" } else { "Release" }
if (-not $Runner) { $Runner = Join-Path $RepoRoot "x64\$NativeConfiguration\EngineUnitTests.exe" }
$Fixtures = Join-Path $PSScriptRoot "Projects"
$TransformOnly = Join-Path $Fixtures "TransformOnly\TransformOnly.dxproj"
$WithScript = Join-Path $Fixtures "WithScript\WithScript.dxproj"
$WithGeometry = Join-Path $Fixtures "WithGeometry\WithGeometry.dxproj"
# WithGeometry のアクティブなシーンは TransformOnly と同じエンティティで、Moved と Scaled に Geometry がある
$GeometrySkippedWarning = "Moved.*Geometry.*game\.bin[\s\S]*Scaled.*Geometry.*game\.bin"
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

function Skip([string]$message)
{
	Write-Host "  SKIP $message"
	$skips.Add($message)
}

# dxforge を実行し、終了コード・標準出力・標準エラーを返す。-Json なら標準出力をJSONとして読み、読めることも確かめる。
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
		Check ($lines.Count -eq 1 -and $null -ne $result.Json) "stdout is exactly one JSON object ($($Arguments -join ' '))" $stdout
	}
	return $result
}

function Expect-Error([string]$name, [string[]]$Arguments, [int]$code, [string]$errorCode)
{
	$r = Invoke-Cli $Arguments -Json
	Check ($r.Code -eq $code) "$name -> exit code $code" "actual $($r.Code); stderr: $($r.Stderr)"
	Check ($null -ne $r.Json -and $r.Json.success -eq $false -and $r.Json.exit_code -eq $code -and $r.Json.error.code -eq $errorCode) "$name -> JSON error.code $errorCode" $r.Stdout
	Check (-not ($r.Stderr -match "Unhandled exception|FailFast")) "$name -> no crash" $r.Stderr
	return $r
}

# game.bin の形式(content::load_game が読むもの)どおりにバイト列を組む
function New-GameBin([object[]]$entities)
{
	$ms = [System.IO.MemoryStream]::new()
	$bw = [System.IO.BinaryWriter]::new($ms)
	$bw.Write([int]$entities.Count)
	foreach ($e in $entities)
	{
		$bw.Write([int]0)
		$bw.Write([int]$(if ($e.Script) { 2 } else { 1 }))
		$bw.Write([int]0) # Transform
		foreach ($v in @($e.Position + $e.Rotation + $e.Scale)) { $bw.Write([single]$v) }
		if ($e.Script)
		{
			$bytes = [System.Text.Encoding]::UTF8.GetBytes($e.Script)
			$bw.Write([int]1) # Script
			$bw.Write([int]$bytes.Length)
			$bw.Write($bytes)
		}
	}
	$bw.Flush()
	return ,$ms.ToArray()
}

function Bytes-Equal([byte[]]$a, [byte[]]$b)
{
	return $null -ne $a -and $null -ne $b -and [System.Linq.Enumerable]::SequenceEqual($a, $b)
}

function Near([double[]]$actual, [double[]]$expected, [double]$tolerance = 1e-5)
{
	if ($actual.Count -ne $expected.Count) { return $false }
	for ($i = 0; $i -lt $actual.Count; ++$i) { if ([Math]::Abs($actual[$i] - $expected[$i]) -gt $tolerance) { return $false } }
	return $true
}

# エンジン(XMQuaternionRotationRollPitchYawFromVector(x=pitch, y=yaw, z=roll))と同じ回転のクォータニオン
function Quaternion([double[]]$euler)
{
	$q = [System.Numerics.Quaternion]::CreateFromYawPitchRoll($euler[1], $euler[0], $euler[2])
	return @($q.X, $q.Y, $q.Z, $q.W)
}

$ActiveSceneEntities = @(
	@{ Name = "Origin"; Position = @(0, 0, 0); Rotation = @(0, 0, 0); Scale = @(1, 1, 1) },
	@{ Name = "Moved"; Position = @(1.5, -2, 3.25); Rotation = @(0, 1.5707964, 0); Scale = @(1, 1, 1) },
	@{ Name = "Scaled"; Position = @(-4, 0.5, 10); Rotation = @(0.5, 0, 0.25); Scale = @(2, 3, 0.5) }
)
$SecondSceneEntities = @(@{ Name = "OnlyInSecond"; Position = @(7, 8, 9); Rotation = @(0, 0, 0); Scale = @(1, 1, 1) })
$WithScriptEntities = @(
	@{ Name = "Plain"; Position = @(0, 1, 0); Rotation = @(0, 0, 0); Scale = @(1, 1, 1) },
	@{ Name = "Scripted"; Position = @(2, 0, 0); Rotation = @(0, 0, 0); Scale = @(1, 1, 1); Script = "Mover" }
)

function Check-Entities([string]$name, $json, [object[]]$expected)
{
	Check ($json.entity_count -eq $expected.Count -and @($json.entities).Count -eq $expected.Count) "$name -> $($expected.Count) entities" ($json | ConvertTo-Json -Depth 5 -Compress)
	for ($i = 0; $i -lt [Math]::Min($expected.Count, @($json.entities).Count); ++$i)
	{
		$a = @($json.entities)[$i]
		$e = $expected[$i]
		$ok = $a.id -eq $i -and $a.name -eq $e.Name -and (Near $a.position $e.Position) -and (Near $a.rotation (Quaternion $e.Rotation)) -and (Near $a.scale $e.Scale)
		Check $ok "$name -> entity $i ($($e.Name)) id, name and transform" ($a | ConvertTo-Json -Compress)
	}
}

if (-not (Test-Path $Cli)) { throw "dxforge.exe がありません: $Cli(./build.ps1 -Target editor -Configuration $Configuration でビルドしてください)" }

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("dxforge_cli_test_" + [Guid]::NewGuid().ToString("N"))
$null = New-Item -ItemType Directory -Force $work
try
{
	Write-Host "1. arguments"
	$r = Invoke-Cli @("--help")
	Check ($r.Code -eq 0 -and $r.Stdout -match "--load-project" -and $r.Stdout -match "33") "--help prints the usage and the exit codes"
	$null = Expect-Error "no --load-project" @("--frames", "1") 2 "invalid_arguments"
	$null = Expect-Error "no --frames" @("--load-project", $TransformOnly) 2 "invalid_arguments"
	$null = Expect-Error "negative --frames" @("--load-project", $TransformOnly, "--frames", "-1") 2 "invalid_arguments"
	$null = Expect-Error "non-numeric --frames" @("--load-project", $TransformOnly, "--frames", "abc") 2 "invalid_arguments"
	$null = Expect-Error "--frames without a value" @("--load-project", $TransformOnly, "--frames") 2 "invalid_arguments"
	$null = Expect-Error "negative --dt" @("--load-project", $TransformOnly, "--frames", "1", "--dt", "-0.1") 2 "invalid_arguments"
	$null = Expect-Error "unknown argument" @("--load-project", $TransformOnly, "--frames", "1", "--bogus") 2 "invalid_arguments"
	$null = Expect-Error "--export-game-bin with --frames" @("--load-project", $TransformOnly, "--frames", "1", "--export-game-bin", (Join-Path $work "x.bin")) 2 "invalid_arguments"

	Write-Host "2. broken projects"
	$xml = [System.IO.File]::ReadAllText($TransformOnly)
	$null = Expect-Error "missing .dxproj" @("--load-project", (Join-Path $work "missing.dxproj"), "--export-game-bin", (Join-Path $work "x.bin")) 30 "project_not_found"
	$broken = Join-Path $work "broken.dxproj"
	[System.IO.File]::WriteAllText($broken, $xml.Substring(0, [int]($xml.Length / 2)))
	$null = Expect-Error "truncated XML" @("--load-project", $broken, "--export-game-bin", (Join-Path $work "x.bin")) 31 "project_invalid"
	$notProject = Join-Path $work "not_project.dxproj"
	[System.IO.File]::WriteAllText($notProject, "<Foo xmlns=`"http://example.com`"/>")
	$null = Expect-Error "XML that is not a project" @("--load-project", $notProject, "--export-game-bin", (Join-Path $work "x.bin")) 31 "project_invalid"
	$empty = Join-Path $work "empty.dxproj"
	[System.IO.File]::WriteAllText($empty, "")
	$null = Expect-Error "empty file" @("--load-project", $empty, "--export-game-bin", (Join-Path $work "x.bin")) 31 "project_invalid"
	$noScenes = Join-Path $work "no_scenes.dxproj"
	[System.IO.File]::WriteAllText($noScenes, [regex]::Replace($xml, "<Scenes>.*</Scenes>", ""))
	$null = Expect-Error "no <Scenes>" @("--load-project", $noScenes, "--export-game-bin", (Join-Path $work "x.bin")) 31 "project_invalid"
	$noEntities = Join-Path $work "no_entities_element.dxproj"
	[System.IO.File]::WriteAllText($noEntities, [regex]::Replace($xml, "<GameEntities [^>]*>.*?</GameEntities>", "", "Singleline"))
	$null = Expect-Error "no <GameEntities> in the active scene" @("--load-project", $noEntities, "--export-game-bin", (Join-Path $work "x.bin")) 31 "project_invalid"
	$noActive = Join-Path $work "no_active.dxproj"
	[System.IO.File]::WriteAllText($noActive, $xml.Replace("<IsActive>true</IsActive>", "<IsActive>false</IsActive>"))
	$r = Expect-Error "no active scene" @("--load-project", $noActive, "--export-game-bin", (Join-Path $work "x.bin")) 32 "scene_not_found"
	Check ($r.Json.project -eq $noActive) "the error JSON names the project" $r.Stdout
	$null = Expect-Error "--scene that does not exist" @("--load-project", $TransformOnly, "--scene", "Nope", "--export-game-bin", (Join-Path $work "x.bin")) 32 "scene_not_found"

	Write-Host "3. game.bin bytes (CLI == editor SaveToBinary == format)"
	# エディタの SaveToBinary を、エディタと同じ読み込み(Project.Load)のあとに呼ぶ。エディタのコードを呼ぶ間は、
	# Debug.Assert の発火を FailFast ではなくテストの失敗として数える。
	if ([System.Threading.Thread]::CurrentThread.GetApartmentState() -ne "STA") { throw "Run this script on an STA thread (pwsh -STA)." }
	Add-Type -AssemblyName PresentationFramework
	Add-Type -TypeDefinition @'
using System.Collections.Generic;
using System.Diagnostics;
namespace DXForgeCliTest
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
	$assertRecorder = [DXForgeCliTest.AssertRecorder]::new()
	$listeners = @([System.Diagnostics.Trace]::Listeners)
	[System.Diagnostics.Trace]::Listeners.Clear()
	[void][System.Diagnostics.Trace]::Listeners.Add($assertRecorder)
	try
	{
		$editor = [System.Reflection.Assembly]::LoadFrom((Join-Path $OutDir "DXForgeEditor.dll"))
		$flags = [System.Reflection.BindingFlags]"Public,NonPublic,Static,Instance"
		if (-not [System.Windows.Application]::Current) { $null = [System.Windows.Application]::new() }
		$environment = $editor.GetType("DXForgeEditor.GameProject.ProjectLoadEnvironment", $true).GetProperty("Headless", $flags)
		Check ($environment.GetValue($null) -eq $false) "ProjectLoadEnvironment.Headless is false by default (the editor never sets it)"
		$projectType = $editor.GetType("DXForgeEditor.GameProject.Project", $true)
		$environment.SetValue($null, $true)

		function Get-EditorGameBin([string]$dxproj, [string]$name)
		{
			$project = $projectType.GetMethod("Load", $flags).Invoke($null, [object[]]@([string]$dxproj))
			$root = Join-Path $work "editor_$name\"
			$null = New-Item -ItemType Directory -Force (Join-Path $root "x64\Debug")
			$project.Path = $root
			$null = $projectType.GetMethod("SaveToBinary", $flags).Invoke($project, @())
			return ,[System.IO.File]::ReadAllBytes((Join-Path $root "x64\Debug\game.bin"))
		}

		foreach ($case in @(
				@{ Name = "TransformOnly"; Path = $TransformOnly; Entities = $ActiveSceneEntities },
				@{ Name = "WithScript"; Path = $WithScript; Entities = $WithScriptEntities },
				@{ Name = "WithGeometry"; Path = $WithGeometry; Entities = $ActiveSceneEntities; GeometrySkipped = $true }))
		{
			$out = Join-Path $work "cli_$($case.Name)\sub\game.bin"
			$r = Invoke-Cli @("--load-project", $case.Path, "--export-game-bin", $out) -Json
			Check ($r.Code -eq 0 -and $r.Json.success -and $r.Json.mode -eq "export_game_bin" -and $r.Json.entity_count -eq $case.Entities.Count -and $r.Json.game_bin -eq $out) "$($case.Name): --export-game-bin succeeds (creates the folder)" $r.Stdout
			if ($case.GeometrySkipped)
			{
				Check ($r.Stderr -match $GeometrySkippedWarning) "$($case.Name): stderr warns that the Geometry components are not written to game.bin" $r.Stderr
				Check ($null -eq $r.Json.PSObject.Properties["skipped_components"] -and $null -eq $r.Json.PSObject.Properties["skippedComponents"]) "$($case.Name): the JSON has no extra field for the skipped components" $r.Stdout
			}
			else
			{
				Check (-not ($r.Stderr -match "game\.bin.*Geometry|Geometry.*game\.bin")) "$($case.Name): no Geometry warning" $r.Stderr
			}
			$cliBytes = if (Test-Path $out) { [System.IO.File]::ReadAllBytes($out) } else { $null }
			$editorBytes = Get-EditorGameBin $case.Path $case.Name
			$expected = New-GameBin $case.Entities
			Check (Bytes-Equal $editorBytes $expected) "$($case.Name): the editor's SaveToBinary writes the game.bin format ($($expected.Length) bytes)"
			Check (Bytes-Equal $cliBytes $editorBytes) "$($case.Name): CLI game.bin == editor SaveToBinary game.bin"
		}

		# Geometry コンポーネントの読み込みと、保存し直したときに中身が変わらないこと(エンジンにもアセットにも触らない)
		$geometryType = $editor.GetType("DXForgeEditor.Components.Geometry", $true)
		$materialsField = $geometryType.GetField("_materials", $flags)
		function Get-GeometryState($project)
		{
			$state = @{}
			foreach ($entity in @($project.ActiveScene.GameEntities))
			{
				$geometry = @($entity.Components | Where-Object { $_.GetType() -eq $geometryType })
				if ($geometry.Count -eq 0) { $state[$entity.Name] = "none"; continue }
				$materials = @($materialsField.GetValue($geometry[0]))
				$state[$entity.Name] = "$($geometry[0].GeometryGuid)|$($geometry[0].ContentId)|" + (($materials | ForEach-Object {
					"$($_.Material);$($_.BaseColor -join ',');$($_.EmissiveColor -join ',');$($_.EmissiveIntensity);$($_.Metallic);$($_.Roughness);$($_.Inputs -join ',');$($_.InputNames -join ',')" }) -join "/")
			}
			return $state
		}
		$geometryProject = $projectType.GetMethod("Load", $flags).Invoke($null, [object[]]@([string]$WithGeometry))
		$state = Get-GeometryState $geometryProject
		Check ($state["Origin"] -eq "none") "WithGeometry: Origin has no Geometry component" $state["Origin"]
		Check ($state["Moved"] -eq "6f1c2b8e-3d4a-4e5f-9a0b-1c2d3e4f5a6b|-1|11111111-2222-4333-8444-555555555555;0.5,0.25,0.125,1;0,0,0,1;1;0.75;0.5;99999999-8888-4777-8666-555555555555;BaseColor") "WithGeometry: Moved has the saved geometry and per-submesh material" $state["Moved"]
		Check ($state["Scaled"] -eq "0a9b8c7d-6e5f-4a3b-8c2d-1e0f9a8b7c6d|-1|") "WithGeometry: Scaled has the saved geometry and no saved materials" $state["Scaled"]
		Check (@($geometryProject.ActiveScene.GameEntities | Where-Object { $_.IsActive }).Count -eq 0) "WithGeometry: headless loading creates no entity in the engine"
		$resaved = Join-Path $work "resaved.dxproj"
		$serializer = $editor.GetType("DXForgeEditor.Utilities.Serializer", $true)
		$null = $serializer.GetMethod("ToFile", $flags).MakeGenericMethod($projectType).Invoke($null, [object[]]@($geometryProject.PSObject.BaseObject, [string]$resaved))
		$reloaded = Get-GeometryState ($projectType.GetMethod("Load", $flags).Invoke($null, [object[]]@([string]$resaved)))
		Check (($reloaded.Keys | ForEach-Object { $reloaded[$_] -eq $state[$_] }) -notcontains $false -and $reloaded.Count -eq $state.Count) "WithGeometry: saving and reloading keeps the geometry and materials" (($reloaded.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join "; ")

		# アセットの登録(AssetRegistry.Reset)がこのプロジェクトのものでないうちは、Geometry.Load() は何もせず(エラーのログも出さず)、
		# 登録のあとの Project.LoadEntitiesWithAssets() に任せる(Codexの指摘, #32)。エンジンにもアセットにも触らない。
		$loggerType = $editor.GetType("DXForgeEditor.Utilities.Logger", $true)
		$messageType = $editor.GetType("DXForgeEditor.Utilities.MessageType", $true)
		$logged = [System.Collections.Generic.List[string]]::new()
		$handler = { param($type, $message) $logged.Add("$($type): $message") }.GetNewClosure() -as ([Action`2].MakeGenericType($messageType, [string]))
		$loggerType.GetEvent("MessageLogged").AddEventHandler($null, $handler)
		try
		{
			$moved = @($geometryProject.ActiveScene.GameEntities | Where-Object { $_.Name -eq "Moved" })[0]
			$movedGeometry = @($moved.Components | Where-Object { $_.GetType() -eq $geometryType })[0]
			$null = $geometryType.GetMethod("Load", [Type[]]@()).Invoke($movedGeometry, @())
			Check (-not $movedGeometry.IsLoaded -and $movedGeometry.ContentId -eq -1 -and $logged.Count -eq 0) "WithGeometry: Geometry.Load() waits for the project's assets to be registered (no upload, no error log)" ($logged -join "; ")
		}
		finally
		{
			$loggerType.GetEvent("MessageLogged").RemoveEventHandler($null, $handler)
		}

		Check ($assertRecorder.Failures.Count -eq 0) "no Debug.Assert fired in the editor code" ($assertRecorder.Failures -join "; ")
	}
	finally
	{
		if ($environment) { $environment.SetValue($null, $false) }
		[System.Diagnostics.Trace]::Listeners.Clear()
		[System.Diagnostics.Trace]::Listeners.AddRange($listeners)
	}

	$r = Invoke-Cli @("--load-project", $TransformOnly, "--scene", "Second Scene", "--export-game-bin", (Join-Path $work "second.bin")) -Json
	Check ($r.Code -eq 0 -and $r.Json.scene -eq "Second Scene" -and (Bytes-Equal ([System.IO.File]::ReadAllBytes((Join-Path $work "second.bin"))) (New-GameBin $SecondSceneEntities))) "--scene picks a non-active scene" $r.Stdout
	$null = New-Item -ItemType Directory -Force (Join-Path $work "is_a_folder.bin")
	$null = Expect-Error "--export-game-bin onto a folder" @("--load-project", $TransformOnly, "--export-game-bin", (Join-Path $work "is_a_folder.bin")) 36 "game_bin_write_failed"

	Write-Host "4. run"
	$null = Expect-Error "scripts in the scene" @("--load-project", $WithScript, "--frames", "1", "--runner", $Cli) 33 "scripts_unsupported"
	$null = Expect-Error "runner not found" @("--load-project", $TransformOnly, "--frames", "1", "--runner", (Join-Path $work "missing.exe")) 34 "runner_not_found"
	$fakeRunner = Join-Path $work "fake_runner.exe"
	[System.IO.File]::WriteAllText($fakeRunner, "not an executable")
	$null = Expect-Error "runner that cannot start" @("--load-project", $TransformOnly, "--frames", "1", "--runner", $fakeRunner) 35 "runner_failed"
	# 既定のランナーのパス(build.ps1 の出力の配置)。dxforge 自身が探すので --runner は渡さない。
	if (-not (Test-Path $Runner))
	{
		if ($RequireRunner) { Check $false "runner exists: $Runner" }
		else { Skip "run cases (no runner at $Runner; build it with ./build.ps1 -Target native -Configuration $NativeConfiguration -Project EngineUnitTests)" }
	}
	else
	{
		$runnerArgs = @()
		if ($Runner -ne (Join-Path $RepoRoot "x64\$NativeConfiguration\EngineUnitTests.exe")) { $runnerArgs = @("--runner", $Runner) }
		$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) "dxforge"
		$tempBefore = if (Test-Path $tempRoot) { @(Get-ChildItem $tempRoot).Count } else { 0 }

		$r = Invoke-Cli (@("--load-project", $TransformOnly, "--frames", "5") + $runnerArgs) -Json
		Check ($r.Code -eq 0 -and $r.Json.success -and $null -eq $r.Json.error) "runs the active scene -> exit code 0" "$($r.Stdout) $($r.Stderr)"
		Check ($r.Json.mode -eq "load_project" -and $r.Json.scene -eq "Sample Scene" -and $r.Json.frames_requested -eq 5 -and $r.Json.frames_run -eq 5 -and $null -eq $r.Json.game_bin -and (Test-Path $r.Json.runner)) "JSON has mode, scene, frames, runner" $r.Stdout
		Check-Entities "active scene" $r.Json $ActiveSceneEntities
		$tempAfter = if (Test-Path $tempRoot) { @(Get-ChildItem $tempRoot).Count } else { 0 }
		Check ($tempAfter -le $tempBefore) "the temporary game.bin is deleted" "before $tempBefore, after $tempAfter"

		$r = Invoke-Cli (@("--load-project", $TransformOnly, "--scene", "Second Scene", "--frames", "0", "--dt", "0.5") + $runnerArgs) -Json
		Check ($r.Code -eq 0 -and $r.Json.frames_run -eq 0 -and $r.Json.dt -eq 0.5) "--frames 0 and --dt are passed to the runner" $r.Stdout
		Check-Entities "second scene" $r.Json $SecondSceneEntities

		$kept = Join-Path $work "kept\game.bin"
		$r = Invoke-Cli (@("--load-project", $TransformOnly, "--frames", "1", "--keep-game-bin", $kept) + $runnerArgs) -Json
		Check ($r.Code -eq 0 -and $r.Json.game_bin -eq $kept -and (Bytes-Equal ([System.IO.File]::ReadAllBytes($kept)) (New-GameBin $ActiveSceneEntities))) "--keep-game-bin keeps the game.bin that was run" $r.Stdout

		# 日本語のフォルダとエンティティ名
		$jpDir = Join-Path $work "日本語のプロジェクト"
		$null = New-Item -ItemType Directory -Force $jpDir
		$jp = Join-Path $jpDir "テスト.dxproj"
		[System.IO.File]::WriteAllText($jp, $xml.Replace("<a:Name>Scaled</a:Name>", "<a:Name>拡大したもの</a:Name>"))
		$r = Invoke-Cli (@("--load-project", $jp, "--frames", "1") + $runnerArgs) -Json
		Check ($r.Code -eq 0 -and $r.Json.project -eq $jp -and @($r.Json.entities)[2].name -eq "拡大したもの") "Japanese path and entity name" $r.Stdout

		# Geometry のあるシーン: Geometry は game.bin に書かないので、TransformOnly と同じエンティティが動く
		$r = Invoke-Cli (@("--load-project", $WithGeometry, "--frames", "1") + $runnerArgs) -Json
		Check ($r.Code -eq 0 -and $r.Json.success -and $null -eq $r.Json.error -and $r.Json.frames_run -eq 1) "WithGeometry: runs -> exit code 0" "$($r.Stdout) $($r.Stderr)"
		Check ($r.Stderr -match $GeometrySkippedWarning) "WithGeometry: stderr warns that the Geometry components are skipped" $r.Stderr
		Check-Entities "WithGeometry" $r.Json $ActiveSceneEntities

		$r = Invoke-Cli (@("--load-project", $TransformOnly, "--frames", "2") + $runnerArgs)
		Check ($r.Code -eq 0 -and $r.Stdout -match "dxforge: OK, scene Sample Scene, 2 frames, 3 entities" -and $r.Stdout -match "\(Moved\)") "human-readable output" $r.Stdout
		$r = Invoke-Cli @("--load-project", $WithScript, "--frames", "1")
		Check ($r.Code -eq 33 -and $r.Stdout -match "FAILED \(exit code 33\) scripts_unsupported") "human-readable error" $r.Stdout
	}
}
finally
{
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
Write-Host "All load_project_cli_test checks passed."
exit 0
