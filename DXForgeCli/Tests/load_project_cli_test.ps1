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
         読み込んだ GameEntity の EntityId はすべて INVALID_ID(-1)で、IsEnabled の要素がなければ有効(#165)。
      4. 実行(ランナー EngineUnitTests.exe が必要): 終了コード0、エンティティの数・名前・Transform(回転はクォータニオン)が
         期待どおり。--scene、--dt、--frames 0、--keep-game-bin、日本語のパスと名前、JSONなしの出力、一時フォルダの後始末。
      5. スクリプトのあるシーン(33)、ランナーがない(34)、ランナーを起動できない(35)。
      6. Geometry コンポーネント(Primal Day 079, #32)のあるシーン: 読み込める(保存したジオメトリとマテリアルを読み、
         保存し直しても変わらない)。game.bin には Geometry を書かない(同じエンティティで Geometry のない TransformOnly と
         1 バイトも違わない)ので書き出しも実行も終了コード 0 で、標準エラーに飛ばした警告が出る。JSON の形は変わらない。
      7. Primal Day 081(#185): シーンの名前の変更の undo/redo。メッシュのアセットのドロップでのエンティティの作成(メッシュでないもの・
         プロジェクトの外のものは作らない。undo/redo)。エディタの経路の Project.Load は、読めないファイルで例外を投げず null を返す。
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

		# DataContractSerializer はフィールドの初期化子を実行しないので、読み込んだ GameEntity の EntityId が
		# INVALID_ID(-1)ではなく 0(有効な ID)になっていた(#165)。エンジンに作っていないエンティティはすべて -1。
		# アクティブでないシーン(TransformOnly の 2 つ目)のエンティティも確かめる(エディタでもエンジンに作らない)。
		foreach ($case in @(@{ Name = "TransformOnly"; Path = $TransformOnly }, @{ Name = "WithScript"; Path = $WithScript }, @{ Name = "WithGeometry"; Path = $WithGeometry }))
		{
			$loaded = $projectType.GetMethod("Load", $flags).Invoke($null, [object[]]@([string]$case.Path))
			$entities = @($loaded.Scenes | ForEach-Object { $_.GameEntities })
			$ids = ($entities | ForEach-Object { "$($_.Name)=$($_.EntityId)" }) -join ", "
			Check ($entities.Count -gt 0 -and @($entities | Where-Object { $_.EntityId -ne -1 }).Count -eq 0) "$($case.Name): every deserialized GameEntity has EntityId INVALID_ID (-1) (#165)" $ids
		}
		# IsEnabled の要素がない(古い).dxproj でも、新しく作るエンティティと同じく有効(true)として読む(#165)
		$noIsEnabled = Join-Path $work "no_is_enabled.dxproj"
		[System.IO.File]::WriteAllText($noIsEnabled, [regex]::Replace([System.IO.File]::ReadAllText($TransformOnly), "<a:IsEnabled>(true|false)</a:IsEnabled>", ""))
		$loaded = $projectType.GetMethod("Load", $flags).Invoke($null, [object[]]@([string]$noIsEnabled))
		$entities = @($loaded.Scenes | ForEach-Object { $_.GameEntities })
		Check ($entities.Count -gt 0 -and @($entities | Where-Object { -not $_.IsEnabled -or $_.EntityId -ne -1 }).Count -eq 0) "no <IsEnabled>: every GameEntity is enabled and has EntityId -1 (#165)" (($entities | ForEach-Object { "$($_.Name)=$($_.IsEnabled)/$($_.EntityId)" }) -join ", ")

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

			# 既定のマテリアル(プロジェクトの AssetRegistry には登録されない)の GUID を保存したマテリアルは、
			# 「見つからない」の警告を出さずに既定のマテリアルを使う(GUI確認で見つかったもの, #32)。見つからない GUID なら警告する。
			$defaultMaterialProperty = $editor.GetType("DXForgeEditor.Content.DefaultAssets", $true).GetProperty("DefaultMaterial", $flags)
			$previousDefaultMaterial = $defaultMaterialProperty.GetValue($null)
			$defaultMaterial = [Activator]::CreateInstance($editor.GetType("DXForgeEditor.Content.AssetInfo", $true))
			$defaultMaterial.Type = [Enum]::Parse($editor.GetType("DXForgeEditor.Content.AssetType", $true), "Material")
			$defaultMaterial.Guid = [Guid]::NewGuid()
			$defaultMaterial.FullPath = Join-Path $work "DefaultMaterial.asset"
			$defaultMaterialProperty.SetValue($null, $defaultMaterial)
			try
			{
				$dataType = $editor.GetType("DXForgeEditor.Components.AppliedMaterialData", $true)
				foreach ($case in @(@{ Guid = $defaultMaterial.Guid; Warns = $false; Name = "the default material's GUID" }, @{ Guid = [Guid]::NewGuid(); Warns = $true; Name = "an unknown GUID" }))
				{
					$logged.Clear()
					$data = [Activator]::CreateInstance($dataType)
					$dataType.GetProperty("Material").SetValue($data, $case.Guid)
					$material = $dataType.GetMethod("ToAppliedMaterial").Invoke($data, @())
					$warned = @($logged | Where-Object { $_ -match "^Warning: .*$($case.Guid)" }).Count -gt 0
					Check ($null -ne $material -and $material.MaterialGuid -eq $defaultMaterial.Guid -and $warned -eq $case.Warns) "AppliedMaterialData with $($case.Name) -> the default material, missing-material warning: $($case.Warns)" ($logged -join "; ")
				}
			}
			finally
			{
				$defaultMaterialProperty.SetValue($null, $previousDefaultMaterial)
			}

			# Day 080(#32): ドロップでの差し替えの undo は、ジオメトリとサブメッシュごとのマテリアルを戻す
			# (GetMaterialsData を取っておき、SetGeometry(guid, materials) で戻す)。ヘッドレスではエンティティがアクティブでないので、
			# エンジンにもアセットにも触らない。
			$logged.Clear()
			$movedBefore = (Get-GeometryState $geometryProject)["Moved"]
			$savedMaterials = $geometryType.GetMethod("GetMaterialsData").Invoke($movedGeometry, @())
			$otherGuid = [Guid]::NewGuid()
			$setGeometry = $geometryType.GetMethod("SetGeometry")
			$null = $setGeometry.Invoke($movedGeometry, [object[]]@($otherGuid, $null))
			Check ($movedGeometry.GeometryGuid -eq $otherGuid -and @($materialsField.GetValue($movedGeometry)).Count -eq 0 -and -not $moved.IsActive) "Geometry.SetGeometry(guid): the new geometry starts with the default materials (no saved data), the entity stays inactive" (Get-GeometryState $geometryProject)["Moved"]
			$null = $setGeometry.Invoke($movedGeometry, [object[]]@([Guid]::Parse($movedBefore.Split("|")[0]), $savedMaterials))
			Check ((Get-GeometryState $geometryProject)["Moved"] -eq $movedBefore -and $logged.Count -eq 0) "Geometry.SetGeometry(guid, materials) (undo) restores the geometry and the per-submesh materials" "$((Get-GeometryState $geometryProject)["Moved"]) / $($logged -join '; ')"

			# 複数選択: どちらもジオメトリを読み込めていない(ContentId が同じ -1)ときは MULTI VALUE にしない(Primal と違う, #32)
			$entityType = $editor.GetType("DXForgeEditor.Components.GameEntity", $true)
			$selection = [Activator]::CreateInstance([System.Collections.Generic.List`1].MakeGenericType($entityType))
			foreach ($name in @("Moved", "Scaled")) { $selection.Add(@($geometryProject.ActiveScene.GameEntities | Where-Object { $_.Name -eq $name })[0]) }
			$msEntity = [Activator]::CreateInstance($editor.GetType("DXForgeEditor.Components.MSGameEntity", $true), $flags, $null, [object[]]@(, $selection), $null)
			$msGeometry = $msEntity.GetType().GetMethod("GetMSComponent").MakeGenericMethod($editor.GetType("DXForgeEditor.Components.MSGeometry", $true)).Invoke($msEntity, @())
			Check ($null -ne $msGeometry -and -not $msGeometry.IsMultiValue -and $null -eq $msGeometry.GeometryWithMaterials -and $msGeometry.GeometryGuid -eq [Guid]::Empty) "MSGeometry: two entities whose geometry is not loaded -> not MULTI VALUE, no geometry shown"

			# 既定のジオメトリ(DefaultAssets.DefaultGeometry、AssetRegistry には登録されない)は、GUID が同じなら探さずに使う。
			# Add Component → Geometry は、ComponentFactory でそれを付ける(Primal Day 080)。
			$defaultGeometryProperty = $editor.GetType("DXForgeEditor.Content.DefaultAssets", $true).GetProperty("DefaultGeometry", $flags)
			$previousDefaultGeometry = $defaultGeometryProperty.GetValue($null)
			$defaultGeometry = [Activator]::CreateInstance($editor.GetType("DXForgeEditor.Content.AssetInfo", $true))
			$defaultGeometry.Type = [Enum]::Parse($editor.GetType("DXForgeEditor.Content.AssetType", $true), "Mesh")
			$defaultGeometry.Guid = [Guid]::NewGuid()
			$defaultGeometry.FullPath = Join-Path $work "DefaultCube.asset"
			$defaultGeometryProperty.SetValue($null, $defaultGeometry)
			try
			{
				$findGeometry = $geometryType.GetMethod("FindGeometry", $flags)
				Check ([object]::ReferenceEquals($findGeometry.Invoke($null, [object[]]@($defaultGeometry.Guid)), $defaultGeometry)) "Geometry.FindGeometry(the default geometry's GUID) -> DefaultAssets.DefaultGeometry"
				Check ($null -eq $findGeometry.Invoke($null, [object[]]@([Guid]::Empty)) -and $null -eq $findGeometry.Invoke($null, [object[]]@([Guid]::NewGuid()))) "Geometry.FindGeometry(empty or unknown GUID) -> null"
				$componentTypeEnum = $editor.GetType("DXForgeEditor.Components.ComponentType", $true)
				$factory = $editor.GetType("DXForgeEditor.Components.ComponentFactory", $true)
				$geometryKind = [Enum]::Parse($componentTypeEnum, "Geometry")
				$origin = @($geometryProject.ActiveScene.GameEntities | Where-Object { $_.Name -eq "Origin" })[0]
				$created = $factory.GetMethod("GetCreationFunction", $flags).Invoke($null, @($geometryKind)).Invoke($origin, $defaultGeometry)
				$kind = $factory.GetMethod("ToEnumType", $flags).Invoke($null, @($created))
				Check ($created.GetType() -eq $geometryType -and $created.GeometryGuid -eq $defaultGeometry.Guid -and "$kind" -eq "Geometry" -and [int]$geometryKind -eq 2 -and @($origin.Components | Where-Object { $_.GetType() -eq $geometryType }).Count -eq 0) "ComponentFactory creates a Geometry with the given asset (ComponentType.Geometry = 2)"
			}
			finally
			{
				$defaultGeometryProperty.SetValue($null, $previousDefaultGeometry)
			}

			# Day 081(#185): シーンの名前の変更と、メッシュのアセットのドロップでのエンティティの作成(undo/redo あり)。
			# アクティブでないシーンを使うので、エンジンには触らない。
			$undoRedo = $projectType.GetProperty("UndoRedo", $flags).GetValue($null)
			$currentProperty = $projectType.GetProperty("Current", $flags)
			$layoutProject = $projectType.GetMethod("Load", $flags).Invoke($null, [object[]]@([string]$TransformOnly))
			Check ($null -eq $currentProperty.GetValue($null)) "Project.Current stays null after a headless load"
			Check ($null -ne $layoutProject.UndoCommand -and $null -ne $layoutProject.SaveCommand) "the project commands are set after the load"
			$second = @($layoutProject.Scenes | Where-Object { $_.Name -eq "Second Scene" })[0]
			$rename = $second.RenameCommand
			Check (-not $rename.CanExecute("Second Scene") -and $rename.CanExecute("Renamed")) "Scene.RenameCommand: the same name cannot be executed"
			$rename.Execute("Renamed")
			Check ($second.Name -eq "Renamed" -and $undoRedo.UndoList[$undoRedo.UndoList.Count - 1].Name -eq "Rename scene 'Second Scene' to 'Renamed'") "Scene.RenameCommand renames the scene and adds an undo entry" $second.Name
			$undoRedo.Undo()
			Check ($second.Name -eq "Second Scene") "undo restores the scene name" $second.Name
			$undoRedo.Redo()
			Check ($second.Name -eq "Renamed") "redo renames the scene again" $second.Name
			$renamedFile = Join-Path $work "renamed.dxproj"
			$null = $serializer.GetMethod("ToFile", $flags).MakeGenericMethod($projectType).Invoke($null, [object[]]@($layoutProject.PSObject.BaseObject, [string]$renamedFile))
			$renamedProject = $projectType.GetMethod("Load", $flags).Invoke($null, [object[]]@([string]$renamedFile))
			Check (@($renamedProject.Scenes | Where-Object { $_.Name -eq "Renamed" }).Count -eq 1) "the new scene name survives saving and reloading"

			function New-AssetFile([string]$path, [int]$type, [Guid]$guid)
			{
				$bw = [System.IO.BinaryWriter]::new([System.IO.File]::Create($path))
				$id = $guid.ToByteArray()
				$bw.Write($type); $bw.Write($id.Length); $bw.Write($id); $bw.Write([long]0); $bw.Write(0); $bw.Write(0)
				$bw.Dispose()
			}
			$assetKind = $editor.GetType("DXForgeEditor.Content.AssetType", $true)
			$meshKind = [int][Enum]::Parse($assetKind, "Mesh")
			$content = Join-Path $work "layout_content"
			$null = New-Item -ItemType Directory -Force $content
			$rockGuid = [Guid]::NewGuid()
			$treeGuid = [Guid]::NewGuid()
			New-AssetFile (Join-Path $content "Rock.asset") $meshKind $rockGuid
			New-AssetFile (Join-Path $content "Tree.asset") $meshKind $treeGuid
			New-AssetFile (Join-Path $content "Checker.asset") ([int][Enum]::Parse($assetKind, "Texture")) ([Guid]::NewGuid())
			[System.IO.File]::WriteAllText((Join-Path $content "notes.txt"), "not an asset")
			New-AssetFile (Join-Path $work "Outside.asset") $meshKind ([Guid]::NewGuid())
			$null = $editor.GetType("DXForgeEditor.Content.AssetRegistry", $true).GetMethod("Reset", $flags).Invoke($null, [object[]]@([string]"$content\"))

			$addGeometryEntities = $second.GetType().GetMethod("AddGeometryEntities", $flags)
			$countBefore = $second.GameEntities.Count
			$logged.Clear()
			[string[]]$noMesh = @((Join-Path $content "Checker.asset"), (Join-Path $content "notes.txt"), (Join-Path $content "Gone.asset"))
			$added = $addGeometryEntities.Invoke($second, [object[]]@(, $noMesh))
			Check ($added.Count -eq 0 -and $second.GameEntities.Count -eq $countBefore -and @($logged | Where-Object { $_ -match "^Warning: " }).Count -eq 1) "AddGeometryEntities: a texture, a text file and a missing file create nothing and warn once" ($logged -join "; ")
			$logged.Clear()
			[string[]]$outside = @((Join-Path $work "Outside.asset"))
			$added = $addGeometryEntities.Invoke($second, [object[]]@(, $outside))
			Check ($added.Count -eq 0 -and $second.GameEntities.Count -eq $countBefore -and @($logged | Where-Object { $_ -match "^Warning: .*Outside" }).Count -eq 1) "AddGeometryEntities: a mesh asset outside the project creates nothing and warns" ($logged -join "; ")
			$logged.Clear()
			[string[]]$meshes = @((Join-Path $content "Rock.asset"), (Join-Path $content "Checker.asset"), (Join-Path $content "Tree.asset"))
			$added = $addGeometryEntities.Invoke($second, [object[]]@(, $meshes))
			$names = @($second.GameEntities | Select-Object -Skip $countBefore | ForEach-Object { $_.Name })
			$addedGuids = @($second.GameEntities | Select-Object -Skip $countBefore | ForEach-Object { $entity = $_; @($entity.Components | Where-Object { $_.GetType() -eq $geometryType })[0].GeometryGuid })
			Check ($added.Count -eq 2 -and ($names -join ",") -eq "Rock,Tree" -and $addedGuids[0] -eq $rockGuid -and $addedGuids[1] -eq $treeGuid) "AddGeometryEntities: one entity per mesh asset, named after the asset, with the Geometry" "$($names -join ',') / $($addedGuids -join ',')"
			Check (@($second.GameEntities | Where-Object { $_.IsActive }).Count -eq 0 -and $logged.Count -eq 0) "AddGeometryEntities in an inactive scene creates nothing in the engine, without logging" ($logged -join "; ")
			$undoRedo.Undo()
			Check (($second.GameEntities.Count -eq $countBefore + 1) -and $second.GameEntities[$countBefore].Name -eq "Rock") "undo removes the last dropped entity" "$($second.GameEntities.Count)"
			$undoRedo.Undo()
			Check ($second.GameEntities.Count -eq $countBefore) "a second undo removes the first dropped entity" "$($second.GameEntities.Count)"
			$undoRedo.Redo(); $undoRedo.Redo()
			$names = @($second.GameEntities | Select-Object -Skip $countBefore | ForEach-Object { $_.Name })
			Check (($names -join ",") -eq "Rock,Tree") "redo adds both entities back in order" ($names -join ",")
			$undoRedo.Reset()

			# エディタの経路(Headless ではない): 読めない .dxproj は例外を投げず、エラーのログを出して null を返す(OpenProjectView の経路で落ちないように)。
			$brokenFolder = Join-Path $work "broken_project"
			$null = New-Item -ItemType Directory -Force (Join-Path $brokenFolder "Content")
			$brokenFile = Join-Path $brokenFolder "broken.dxproj"
			[System.IO.File]::WriteAllText($brokenFile, "<Game><not valid")
			$logged.Clear()
			$environment.SetValue($null, $false)
			try
			{
				$brokenResult = $projectType.GetMethod("Load", $flags).Invoke($null, [object[]]@([string]$brokenFile))
				Check ($null -eq $brokenResult -and $null -eq $currentProperty.GetValue($null) -and @($logged | Where-Object { $_ -match "^Error: .*broken\.dxproj" }).Count -ge 1) "editor path: Project.Load of a broken file returns null and logs an error, Project.Current stays null" ($logged -join "; ")
				# Components.Geometry.Load は AssetRegistry.ContentFolder と Project.ContentPath(末尾の区切り文字あり)を比べる。違うとジオメトリを読み込めない
				$registeredFolder = $editor.GetType("DXForgeEditor.Content.AssetRegistry", $true).GetProperty("ContentFolder", $flags).GetValue($null)
				Check ($registeredFolder -ceq "$brokenFolder\Content\") "editor path: Project.Load registers the assets with the same form as Project.ContentPath" "$registeredFolder"
			}
			finally
			{
				$environment.SetValue($null, $true)
				$null = $editor.GetType("DXForgeEditor.Content.ContentWatcher", $true).GetMethod("Reset", $flags).Invoke($null, [object[]]@([string]"", [string]$brokenFolder))
			}
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
