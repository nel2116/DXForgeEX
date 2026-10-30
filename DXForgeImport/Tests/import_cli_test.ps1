<#
.SYNOPSIS
    コマンドラインのアセット・インポーター DXForgeImport の回帰テスト(Issue #10)。

.DESCRIPTION
    ビルド済みの x64\<Configuration>\DXForgeImport.exe を実行して、終了コードと --json の出力を確かめる。
    GUI は使わない(エディタのウィンドウもプロジェクトも開かない)。

    1. 引数・異常系(ContentTools.dll がなくても確かめられるもの。CI ではこれと次の AssetRegistry だけを実行する: -ArgumentsOnly)
       --help、引数の誤り(空白だけの --input/--output を含む)、入力なし、未対応の拡張子、--type と拡張子の不一致、設定の誤り、
       出力先を作れない(パスに使えない文字を含む)。
       ContentTools.dll がない場合は、正しい入力で終了コード7になることも確かめる。
       AssetRegistry の登録(#137): .asset と、.asset 以外のファイル(.fbm フォルダの画像、中身が .asset と同じ .bak)がある
       Content フォルダを、エディタのコード(DXForgeEditor.dll の AssetRegistry.Reset と、ファイルの変更の通知)に登録させ、
       .asset だけが登録されることを確かめる。
       エディタのコードを呼ぶ間は、Trace.Listeners を失敗を記録するだけのリスナーに置き換え、DEBUG を定義したビルドの
       Debug.Assert の発火(デバッガがなければ FailFast でこのプロセスが終わる)を、テストの失敗として数える。
    2. インポート(ContentTools.dll と FBX SDK のサンプルが必要。ローカルで実行する)
       - テクスチャ(DXForgeEditor\Resources\TextureEditor\Checker64.png)と FBX(既定は FBX SDK のサンプル box.fbx)を
         インポートし、終了コード0と、出力した .asset の一覧が返る。設定ファイルと --set が .asset に反映される。
       - 壊れた PNG / FBX で終了コード1と --json のエラーが返り、.asset を書かない(assert のダイアログやクラッシュにならない)。
       - 保存の失敗(出力先の .asset が読み取り専用)で終了コード1になる。-MultiLodFbxFile を指定すると、LODグループが2つ以上ある
         FBXの2つ目以降の .asset を読み取り専用にして、一部だけ保存できた場合も終了コード1になり、書けたファイルが files に残ることを確かめる。
       - FBXの埋め込みテクスチャ: box.fbx に Checker64.png を埋め込んだFBX(ASCII の Video オブジェクト)をテスト中に作ってインポートし、
         埋め込み画像が出力先の <名前>.fbm にコピーされ、テクスチャの Sources がそこを指し(CLI の一時フォルダは終了時に消える)、
         エディタのコードでそこから再インポートできることを確かめる。
       - エディタとの互換: CLI が書いた .asset を、エディタのコード(DXForgeEditor.dll の Texture.Load / Geometry.Load)を
         リフレクションで呼んで読み込める。
       - 同じ入力をエディタのインポートの経路(ContentHelper.Import、エディタのドラッグ&ドロップと同じ関数)で書き出し、
         CLI の出力とバイト単位で比べる。変わって当然の GUID(オフセット8から16バイト)とインポート日時(オフセット24から8バイト)を除く。
         テクスチャは、圧縮データ(とその SHA-256 のハッシュ)のバイト列の代わりに展開した内容を比べる(Compare-AssetFiles の説明を参照)。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。

.PARAMETER ArgumentsOnly
    1 と AssetRegistry の登録だけを実行する(ContentTools.dll と FBX SDK が不要。CI 用)。

.PARAMETER FbxFile
    FBX のテストに使うファイル。既定は FBX SDK 2020.3.7 のサンプル(samples\ConvertScene\box.fbx)。

.PARAMETER MultiLodFbxFile
    LODグループが2つ以上ある FBX(省略可)。指定すると、一部だけ保存できた場合のテストを行う。リポジトリにもFBX SDKのサンプルにも
    該当するものがないので、既定では行わない。

.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeImport/Tests/import_cli_test.ps1
.EXAMPLE
    pwsh -STA -NoProfile -File DXForgeImport/Tests/import_cli_test.ps1 -ArgumentsOnly
#>
[CmdletBinding()]
param(
	[ValidateSet("DebugEditor", "ReleaseEditor")]
	[string]$Configuration = "DebugEditor",

	[switch]$ArgumentsOnly,

	[string]$FbxFile = "C:\Program Files\Autodesk\FBX\FBX SDK\2020.3.7\samples\ConvertScene\box.fbx",

	[string]$MultiLodFbxFile
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$OutDir = Join-Path $RepoRoot "x64\$Configuration"
$Exe = Join-Path $OutDir "DXForgeImport.exe"
$TextureFile = Join-Path $RepoRoot "DXForgeEditor\Resources\TextureEditor\Checker64.png"
$WorkDir = Join-Path ([System.IO.Path]::GetTempPath()) "DXForgeImportTest_$([guid]::NewGuid().ToString('N'))"
$failures = [System.Collections.Generic.List[string]]::new()

function Check([bool]$condition, [string]$message)
{
	Write-Host ($(if ($condition) { "  PASS " } else { "  FAIL " }) + $message)
	if (-not $condition) { $failures.Add($message) }
}

# CLI を実行し、終了コードと --json の結果(パースしたもの)を返す。標準エラー(ログ)は捨てずに -ShowLog で表示できる。
function Invoke-Import([string[]]$Arguments, [switch]$NoJson)
{
	$allArgs = @($Arguments) + $(if ($NoJson) { @() } else { @("--json") })
	$stdoutFile = Join-Path $WorkDir "stdout.txt"
	$stderrFile = Join-Path $WorkDir "stderr.txt"
	$process = Start-Process -FilePath $Exe -ArgumentList ($allArgs | ForEach-Object { if ($_ -match '[\s"]') { '"' + ($_ -replace '"', '\"') + '"' } else { $_ } }) `
		-NoNewWindow -Wait -PassThru -RedirectStandardOutput $stdoutFile -RedirectStandardError $stderrFile
	$stdout = [System.IO.File]::ReadAllText($stdoutFile, [System.Text.Encoding]::UTF8)
	$stderr = [System.IO.File]::ReadAllText($stderrFile, [System.Text.Encoding]::UTF8)
	$json = $null
	if (-not $NoJson)
	{
		try { $json = $stdout | ConvertFrom-Json } catch { Write-Host "    (標準出力をJSONとして読めません: $stdout)" }
	}
	[pscustomobject]@{ ExitCode = $process.ExitCode; Json = $json; Stdout = $stdout; Stderr = $stderr }
}

function Check-Failure([string]$name, [string[]]$Arguments, [int]$expectedCode, [string]$expectedName)
{
	$r = Invoke-Import $Arguments
	Check ($r.ExitCode -eq $expectedCode) "${name}: 終了コード $expectedCode (実際: $($r.ExitCode))"
	Check ($null -ne $r.Json -and $r.Json.success -eq $false -and $r.Json.exitCode -eq $expectedCode -and $r.Json.exitCodeName -eq $expectedName -and @($r.Json.errors).Count -gt 0) `
		"${name}: --json に success=false, exitCode=$expectedCode ($expectedName) とエラーが出る"
	Check ($r.Stderr -match '\[error\]') "${name}: 標準エラーにエラーのログが出る"
	return $r
}

if (-not (Test-Path $Exe)) { throw "$Exe がありません。先に ./build.ps1 -Target editor -Configuration $Configuration でビルドしてください。" }
New-Item -ItemType Directory -Path $WorkDir | Out-Null

try
{
	$hasContentTools = Test-Path (Join-Path $OutDir "ContentTools.dll")
	$outputDir = Join-Path $WorkDir "out"
	$notAFile = Join-Path $WorkDir "file.txt"
	Set-Content -Path $notAFile -Value "not an image"
	$badJson = Join-Path $WorkDir "bad.json"
	Set-Content -Path $badJson -Value "{ not json"
	$unknownKey = Join-Path $WorkDir "unknown.json"
	Set-Content -Path $unknownKey -Value '{ "texture": { "NoSuchSetting": 1 } }'
	$wrongType = Join-Path $WorkDir "wrongtype.json"
	Set-Content -Path $wrongType -Value '{ "geometry": { "SmoothingAngle": "wide" } }'

	Write-Host "== 1. 引数・異常系 [$Configuration] =="
	$r = Invoke-Import @("--help") -NoJson
	Check ($r.ExitCode -eq 0 -and $r.Stdout -match '終了コード') "--help: 終了コード0で、終了コードの一覧を出す"

	Check-Failure "引数なし" @() 2 "InvalidArguments" | Out-Null
	Check-Failure "知らない引数" @("--input", $TextureFile, "--output", $outputDir, "--bogus") 2 "InvalidArguments" | Out-Null
	Check-Failure "--output の値がない" @("--input", $TextureFile, "--output") 2 "InvalidArguments" | Out-Null
	Check-Failure "--type の誤り" @("--input", $TextureFile, "--output", $outputDir, "--type", "audio") 2 "InvalidArguments" | Out-Null
	Check-Failure "入力ファイルがない" @("--input", (Join-Path $WorkDir "missing.png"), "--output", $outputDir) 3 "InputNotFound" | Out-Null
	Check-Failure "未対応の拡張子" @("--input", $notAFile, "--output", $outputDir) 4 "UnsupportedFormat" | Out-Null
	Check-Failure "--type と拡張子の不一致" @("--input", $TextureFile, "--output", $outputDir, "--type", "geometry") 4 "UnsupportedFormat" | Out-Null
	Check-Failure "設定ファイルがJSONでない" @("--input", $TextureFile, "--output", $outputDir, "--settings", $badJson) 5 "InvalidSettings" | Out-Null
	Check-Failure "設定ファイルに知らない項目" @("--input", $TextureFile, "--output", $outputDir, "--settings", $unknownKey) 5 "InvalidSettings" | Out-Null
	Check-Failure "設定ファイルの型の誤り" @("--input", $TextureFile, "--output", $outputDir, "--settings", $wrongType) 5 "InvalidSettings" | Out-Null
	Check-Failure "設定ファイルがない" @("--input", $TextureFile, "--output", $outputDir, "--settings", (Join-Path $WorkDir "missing.json")) 5 "InvalidSettings" | Out-Null
	Check-Failure "--set の形の誤り" @("--input", $TextureFile, "--output", $outputDir, "--set", "Compress") 5 "InvalidSettings" | Out-Null
	Check-Failure "--set の値の誤り" @("--input", $TextureFile, "--output", $outputDir, "--set", "Dimension=Texture4D") 5 "InvalidSettings" | Out-Null
	Check-Failure "--set の項目が入力の種類に合わない" @("--input", $TextureFile, "--output", $outputDir, "--set", "SmoothingAngle=90") 5 "InvalidSettings" | Out-Null
	Check-Failure "--input が空白だけ" @("--input", " ", "--output", $outputDir) 2 "InvalidArguments" | Out-Null
	$r = Check-Failure "--output が空白だけ" @("--input", $TextureFile, "--output", " ") 2 "InvalidArguments"
	Check ($null -ne $r.Json -and $r.Json.output -eq " ") "--output が空白だけ: --json の output には引数をそのまま出す(検証前のパスを解決しない)"
	Check-Failure "出力先を作れない" @("--input", $TextureFile, "--output", (Join-Path $notAFile "sub")) 6 "OutputError" | Out-Null
	Check-Failure "出力先のパスに使えない文字" @("--input", $TextureFile, "--output", (Join-Path $WorkDir "a|b")) 6 "OutputError" | Out-Null
	Check (-not (Test-Path $outputDir)) "異常系では出力先に何も書かない"

	if (-not $hasContentTools)
	{
		Check-Failure "ContentTools.dll がない" @("--input", $TextureFile, "--output", $outputDir) 7 "ContentToolsUnavailable" | Out-Null
	}

	# ここからはエディタのコード(DXForgeEditor.dll)をこのプロセスに読み込んで呼ぶ。
	# DEBUG を定義したビルドの Debug.Assert は、デバッガがなければプロセスを FailFast で終わらせるので、
	# Trace.Listeners を失敗を記録するだけのリスナーに置き換え、assert の発火をテストの失敗として数える。
	if ([System.Threading.Thread]::CurrentThread.GetApartmentState() -ne "STA") { throw "Run this script on an STA thread (pwsh -STA)." }
	Add-Type -AssemblyName PresentationFramework
	Add-Type -TypeDefinition @'
using System.Collections.Generic;
using System.Diagnostics;
namespace DXForgeImportTest
{
	public class AssertRecorder : TraceListener
	{
		public readonly List<string> Failures = new List<string>();
		public override void Write(string message) { }
		public override void WriteLine(string message) { }
		public override void Fail(string message, string detailMessage) { Failures.Add(message + " " + detailMessage + " @ " + new StackTrace(true)); }
	}
}
'@
	$assertRecorder = [DXForgeImportTest.AssertRecorder]::new()
	[System.Diagnostics.Trace]::Listeners.Clear()
	[void][System.Diagnostics.Trace]::Listeners.Add($assertRecorder)
	$editor = [System.Reflection.Assembly]::LoadFrom((Join-Path $OutDir "DXForgeEditor.dll"))
	$flags = [System.Reflection.BindingFlags]"Public,NonPublic,Static,Instance"

	function Check-NoAssert([string]$name, [int]$before)
	{
		$fired = @($assertRecorder.Failures | Select-Object -Skip $before)
		Check ($fired.Count -eq 0) "${name}: エディタのコードの Debug.Assert が発火しない (実際: $($fired.Count) 件)"
		foreach ($f in $fired) { Write-Host "    assert:$(($f -split "`n" | Where-Object { $_ -match 'at DXForgeEditor\.' } | Select-Object -First 1))" }
	}

	# AssetRegistry は Content フォルダの .asset だけを登録する(#137)。ContentTools.dll は要らないので CI でも実行する。
	Write-Host "== AssetRegistry の登録 [$Configuration] =="
	$contentDir = Join-Path $WorkDir "registry\Content"
	$registryAsset = Join-Path $contentDir "registered.asset"
	$fbmDir = Join-Path $contentDir "model.fbm"
	New-Item -ItemType Directory -Path $fbmDir | Out-Null
	# .asset のヘッダ(Asset.WriteAssetFileHeader)だけのファイル: asset type(Texture = 6), GUID の長さと GUID, インポート日時, ハッシュの長さ 0, アイコンの長さ 0
	$header = [System.IO.MemoryStream]::new()
	$writer = [System.IO.BinaryWriter]::new($header)
	$writer.Write([int]6); $writer.Write([int]16); $writer.Write([guid]::NewGuid().ToByteArray()); $writer.Write([DateTime]::Now.ToBinary()); $writer.Write([int]0); $writer.Write([int]0)
	$writer.Flush()
	[System.IO.File]::WriteAllBytes($registryAsset, $header.ToArray())
	# .asset 以外: FBX の埋め込みテクスチャ(.fbm フォルダの画像)と、中身は .asset と同じだが拡張子が違うファイル(バックアップなど)
	$fbmImage = Join-Path $fbmDir "embedded.png"
	Copy-Item $TextureFile $fbmImage
	[System.IO.File]::WriteAllBytes((Join-Path $contentDir "registered.asset.bak"), $header.ToArray())
	$assetRegistry = $editor.GetType("DXForgeEditor.Content.AssetRegistry", $true)
	$registered = $assetRegistry.GetProperty("Assets", $flags).GetValue($null)
	$before = $assertRecorder.Failures.Count
	$assetRegistry.GetMethod("Reset", $flags).Invoke($null, @([string]$contentDir))
	$paths = @($registered | ForEach-Object { $_.FullPath })
	Check ($paths.Count -eq 1 -and $paths[0] -eq $registryAsset) "AssetRegistry: プロジェクトを開いたとき(Reset)、.asset だけが登録される (実際: $($paths -join ', '))"
	Check-NoAssert "AssetRegistry(Reset)" $before
	# ファイルの変更の通知(ContentWatcher.ContentModified)でも、.asset 以外は登録しない
	$onContentModified = $assetRegistry.GetMethod("OnContentModified", $flags)
	$eventArgs = $editor.GetType("DXForgeEditor.Content.ContentModifiedEventArgs", $true)
	$before = $assertRecorder.Failures.Count
	foreach ($modified in @($fbmImage, $fbmDir))
	{
		[void]$onContentModified.Invoke($null, @($null, [Activator]::CreateInstance($eventArgs, @([string]$modified))))
	}
	$paths = @($registered | ForEach-Object { $_.FullPath })
	Check ($paths.Count -eq 1 -and $paths[0] -eq $registryAsset) "AssetRegistry: .fbm の画像とフォルダの変更が通知されても、.asset 以外は登録されない (実際: $($paths -join ', '))"
	Check-NoAssert "AssetRegistry(変更の通知)" $before

	if ($ArgumentsOnly)
	{
		Write-Host "(-ArgumentsOnly: インポートのテストは実行しません)"
	}
	elseif (-not $hasContentTools)
	{
		Check $false "ContentTools.dll がありません: $OutDir(./build.ps1 -Target native -Configuration $Configuration でビルドしてください。FBX SDK が必要)"
	}
	else
	{
		Write-Host "== 2. インポート [$Configuration] =="
		$cliDir = Join-Path $WorkDir "cli"

		# テクスチャ
		$r = Invoke-Import @("--input", $TextureFile, "--output", $cliDir, "--type", "texture")
		$cliTexture = Join-Path $cliDir "checker64.asset"
		Check ($r.ExitCode -eq 0 -and $r.Json.success -and $r.Json.type -eq "texture") "テクスチャ: 終了コード0、success=true (実際: $($r.ExitCode))"
		Check (@($r.Json.files).Count -eq 1 -and $r.Json.files[0] -eq $cliTexture -and (Test-Path $cliTexture)) "テクスチャ: files に $cliTexture が1つだけ入り、ファイルがある"
		Check ($r.Json.settings.Compress -eq $true -and $r.Json.settings.FormatIndex -eq 0 -and @($r.Json.settings.Sources)[0] -eq $TextureFile) "テクスチャ: 設定を指定しなければエディタの既定値で、Sources に入力が入る"

		# テクスチャ(設定ファイルと --set。--set が後に適用される)
		$settingsFile = Join-Path $WorkDir "import.json"
		Set-Content -Path $settingsFile -Value '{ "texture": { "Compress": false, "MipLevels": 3 }, "geometry": { "SmoothingAngle": 90 } }'
		$settingsDir = Join-Path $WorkDir "settings"
		$r = Invoke-Import @("--input", $TextureFile, "--output", $settingsDir, "--settings", $settingsFile, "--set", "mipLevels=2")
		Check ($r.ExitCode -eq 0 -and $r.Json.settings.Compress -eq $false -and $r.Json.settings.MipLevels -eq 2) "設定: 設定ファイル(Compress=false)と --set(MipLevels=2、設定ファイルの3を上書き)が反映される"
		$settingsTexture = Join-Path $settingsDir "checker64.asset"


		# FBX
		$cliGeometry = $null
		if (Test-Path $FbxFile)
		{
			$r = Invoke-Import @("--input", $FbxFile, "--output", $cliDir, "--settings", $settingsFile)
			$cliGeometry = Join-Path $cliDir (([System.IO.Path]::GetFileNameWithoutExtension($FbxFile)).ToLower() + ".asset")
			Check ($r.ExitCode -eq 0 -and $r.Json.success -and $r.Json.type -eq "geometry") "FBX: 終了コード0、success=true (実際: $($r.ExitCode))"
			Check (@($r.Json.files) -contains $cliGeometry -and (Test-Path $cliGeometry)) "FBX: files に $cliGeometry が入り、ファイルがある"
			Check ($r.Json.settings.SmoothingAngle -eq 90) "FBX: 設定ファイルの geometry の節(SmoothingAngle=90)が反映される"
		}
		else
		{
			Check $false "FBX のテスト用ファイルがありません: $FbxFile(-FbxFile で指定してください)"
		}

		# 壊れたファイル
		$brokenDir = Join-Path $WorkDir "broken"
		$random = [byte[]]::new(4096)
		[System.Random]::new(10).NextBytes($random)
		$brokenPng = Join-Path $WorkDir "broken.png"
		$brokenFbx = Join-Path $WorkDir "broken.fbx"
		[System.IO.File]::WriteAllBytes($brokenPng, $random)
		[System.IO.File]::WriteAllBytes($brokenFbx, $random)
		Check-Failure "壊れたPNG" @("--input", $brokenPng, "--output", $brokenDir) 1 "ImportFailed" | Out-Null
		Check-Failure "壊れたFBX" @("--input", $brokenFbx, "--output", $brokenDir) 1 "ImportFailed" | Out-Null
		Check (@(Get-ChildItem $brokenDir -ErrorAction SilentlyContinue).Count -eq 0) "壊れたファイルでは .asset を書かない"

		# 保存の失敗(出力先の .asset が読み取り専用で上書きできない)
		$readOnlyDir = Join-Path $WorkDir "readonly"
		New-Item -ItemType Directory -Path $readOnlyDir | Out-Null
		$readOnlyAsset = Join-Path $readOnlyDir "checker64.asset"
		Set-Content -Path $readOnlyAsset -Value "read only"
		(Get-Item $readOnlyAsset).IsReadOnly = $true
		$r = Check-Failure "保存の失敗(読み取り専用の .asset)" @("--input", $TextureFile, "--output", $readOnlyDir) 1 "ImportFailed"
		Check (@($r.Json.files).Count -eq 0 -and (Get-Content $readOnlyAsset) -eq "read only") "保存の失敗: files は空で、既存のファイルはそのまま"
		(Get-Item $readOnlyAsset).IsReadOnly = $false

		# 一部だけ保存できた場合(LODグループが2つ以上あるFBXが必要)
		if ($MultiLodFbxFile)
		{
			$partialDir = Join-Path $WorkDir "partial"
			$r = Invoke-Import @("--input", $MultiLodFbxFile, "--output", $partialDir)
			$all = @($r.Json.files)
			Check ($r.ExitCode -eq 0 -and $all.Count -ge 2) "一部の保存: 準備として、$MultiLodFbxFile から .asset が2つ以上できる (実際: $($all.Count))"
			if ($all.Count -ge 2)
			{
				Remove-Item $all[0]
				foreach ($file in $all[1..($all.Count - 1)]) { (Get-Item $file).IsReadOnly = $true }
				$r = Check-Failure "一部の保存(2つ目以降の .asset が読み取り専用)" @("--input", $MultiLodFbxFile, "--output", $partialDir) 1 "ImportFailed"
				Check (@($r.Json.files).Count -eq 1 -and $r.Json.files[0] -eq $all[0] -and (Test-Path $all[0])) "一部の保存: 書けたファイル(1つ目)が files に残る"
				foreach ($file in $all[1..($all.Count - 1)]) { (Get-Item $file).IsReadOnly = $false }
			}
		}
		else
		{
			Write-Host "  (-MultiLodFbxFile を指定していないので、一部だけ保存できた場合のテストは行いません)"
		}

		# FBXの埋め込みテクスチャ: ASCII の box.fbx に Checker64.png を Video オブジェクトとして埋め込んだFBXを作る
		$embeddedFbx = $null
		$embeddedDir = Join-Path $WorkDir "embedded"
		$embeddedImage = Join-Path $embeddedDir "embedded_box.fbm\embedded_checker.png"
		$embeddedAsset = Join-Path $embeddedDir "embedded_checker.asset"
		if ((Test-Path $FbxFile) -and (Get-Content $FbxFile -TotalCount 1) -match '^; FBX 7')
		{
			$lines = [System.Collections.Generic.List[string]](Get-Content $FbxFile)
			$base64 = [Convert]::ToBase64String([System.IO.File]::ReadAllBytes($TextureFile))
			$connections = $lines.IndexOf("Connections:  {")
			$objectsEnd = $connections - 1
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
			# Definitions に Video の型を足す(ないと FBX SDK が埋め込み画像を取り出さない)
			$definitions = $lines.IndexOf("Definitions:  {")
			$count = $definitions + 1
			while ($lines[$count] -notmatch '^\s+Count: (\d+)$') { ++$count }
			$lines[$count] = "    Count: $([int]$Matches[1] + 1)"
			$lines.InsertRange($count + 1, [string[]]@('    ObjectType: "Video" {', '        Count: 1', '    }'))
			$embeddedFbx = Join-Path $WorkDir "embedded_box.fbx"
			[System.IO.File]::WriteAllLines($embeddedFbx, $lines)

			$r = Invoke-Import @("--input", $embeddedFbx, "--output", $embeddedDir)
			$embedded = @($r.Json.embeddedTextures)
			Check ($r.ExitCode -eq 0 -and $embedded.Count -eq 1 -and $embedded[0].success -and @($embedded[0].files)[0] -eq $embeddedAsset -and (Test-Path $embeddedAsset)) `
				"埋め込みテクスチャ: 終了コード0で、embeddedTextures に1つ成功が入り、$embeddedAsset ができる"
			Check ($embedded.Count -eq 1 -and $embedded[0].input -eq $embeddedImage -and (Test-Path $embeddedImage)) `
				"埋め込みテクスチャ: 埋め込み画像が出力先の embedded_box.fbm にコピーされ、CLIの終了後も残る"
		}
		else
		{
			Check $false "埋め込みテクスチャのテスト用FBXを作れません($FbxFile が ASCII の FBX 7 ではない)"
		}
		Check (-not (Test-Path (Join-Path ([System.IO.Path]::GetTempPath()) "DXForgeImport"))) "FBX の一時フォルダ(%TEMP%\DXForgeImport\<id> と、空になった %TEMP%\DXForgeImport)を残さない"

		# エディタのコードで読み込む・エディタの経路で書き出して比べる(このプロセスにエディタのアセンブリを読み込む)
		Write-Host "== 3. エディタとの互換 [$Configuration] =="
		$assertsBeforeEditor = $assertRecorder.Failures.Count
		$app = [System.Windows.Application]::Current
		if ($null -eq $app) { $app = [System.Windows.Application]::new() }
		[void][System.Runtime.InteropServices.NativeLibrary]::Load((Join-Path $OutDir "ContentTools.dll"))
		if ($Configuration -eq "DebugEditor")
		{
			# Debugのassertはモーダルなダイアログを出して止まるため、標準エラーに出してabortするようにする
			Add-Type -Namespace DXForgeImportTest -Name Crt -MemberDefinition @'
[DllImport("ucrtbased.dll")] public static extern int _set_error_mode(int mode);
[DllImport("ucrtbased.dll")] public static extern uint _set_abort_behavior(uint flags, uint mask);
'@
			[void][DXForgeImportTest.Crt]::_set_error_mode(1)
			[void][DXForgeImportTest.Crt]::_set_abort_behavior(0, 3)
		}
		# ジオメトリのアイコン生成(GeometryView)がエディタのリソースを使うので、DXForgeImport と同じくエディタの EditorColors.xaml をマージする。
		$app.Resources.MergedDictionaries.Add([System.Windows.ResourceDictionary]@{ Source = [Uri]"pack://application:,,,/DXForgeEditor;component/Dictionaries/EditorColors.xaml" })
		$textureType = $editor.GetType("DXForgeEditor.Content.Texture", $true)
		$geometryType = $editor.GetType("DXForgeEditor.Content.Geometry", $true)
		$textureSettingsType = $editor.GetType("DXForgeEditor.Content.TextureImportSettings", $true)
		$geometrySettingsType = $editor.GetType("DXForgeEditor.Content.GeometryImportSettings", $true)
		$contentHelper = $editor.GetType("DXForgeEditor.ContentHelper", $true)
		$environment = $editor.GetType("DXForgeEditor.Content.ContentImportEnvironment", $true)
		$contentToolsApi = $editor.GetType("DXForgeEditor.DllWrappers.ContentToolsAPI", $true)
		$editorImport = $contentHelper.GetMethods($flags) | Where-Object { $_.Name -eq "Import" -and $_.GetParameters().Count -eq 3 -and $_.GetParameters()[0].ParameterType -eq [string] }

		function Load-Asset($type, [string]$file)
		{
			$asset = [Activator]::CreateInstance($type)
			$loaded = $type.GetMethod("Load").Invoke($asset, @($file))
			return @($loaded, $asset)
		}

		$loaded, $texture = Load-Asset $textureType $cliTexture
		Check ($loaded -and $texture.Width -eq 64 -and $texture.Height -eq 64 -and $texture.ImportSettings.Sources[0] -eq $TextureFile) "互換: CLI のテクスチャを Texture.Load で読め、64x64 で Sources が入力のパス"
		$loaded, $texture = Load-Asset $textureType $settingsTexture
		Check ($loaded -and $texture.ImportSettings.Compress -eq $false -and $texture.ImportSettings.MipLevels -eq 2 -and $texture.MipLevels -eq 2) "互換: 設定を変えた CLI のテクスチャを読め、設定(Compress=false, MipLevels=2)とミップ数が反映されている"
		if ($cliGeometry)
		{
			$loaded, $geometry = Load-Asset $geometryType $cliGeometry
			$lodGroup = if ($loaded) { $geometryType.GetMethod("GetLODGroup").Invoke($geometry, @(0)) } else { $null }
			Check ($loaded -and $null -ne $lodGroup -and $lodGroup.LODs.Count -gt 0 -and $lodGroup.LODs[0].Meshes[0].VertexCount -gt 0 -and $geometry.ImportSettings.SmoothingAngle -eq 90) `
				"互換: CLI のジオメトリを Geometry.Load で読め、LOD とメッシュ(頂点)があり、設定(SmoothingAngle=90)が入っている"
		}
		if ($embeddedFbx -and (Test-Path $embeddedAsset))
		{
			# エディタの「再インポート」(TextureEditor.OnReimportCommand)と同じく、読み込んだテクスチャで Import(Texture.FullPath) を呼ぶ
			# (ContentToolsAPI.Import はインポート設定の Sources の画像を読む)
			$loaded, $texture = Load-Asset $textureType $embeddedAsset
			$source = if ($loaded) { $texture.ImportSettings.Sources[0] } else { $null }
			Check ($loaded -and $source -eq $embeddedImage) "互換: 埋め込みテクスチャの Sources が出力先の embedded_box.fbm の画像を指す(一時フォルダではない)"
			Check ($loaded -and $textureType.GetMethod("Import").Invoke($texture, @([string]$texture.FullPath))) "互換: 埋め込みテクスチャを Sources から再インポートできる(エディタの Texture.Import)"
		}

		# エディタの経路(ContentHelper.Import)で同じ入力を書き出し、CLI の出力と比べる
		$editorDir = Join-Path $WorkDir "editor"
		New-Item -ItemType Directory -Path $editorDir | Out-Null
		$environment.GetProperty("TempFolder").SetValue($null, (Join-Path $WorkDir "editor_temp") + "\")
		# .asset のヘッダ(Asset.WriteAssetFileHeader): asset type(int32), GUID の長さ(int32 = 16), GUID(16バイト), インポート日時(int64),
		# ハッシュの長さ(int32), ハッシュ, アイコンの長さ(int32), アイコン(PNG)。その後に種類ごとの本体が続く。
		function Get-AssetLayout([byte[]]$bytes)
		{
			$hashSize = [System.BitConverter]::ToInt32($bytes, 32)
			$iconSizeOffset = 36 + $hashSize
			$iconSize = [System.BitConverter]::ToInt32($bytes, $iconSizeOffset)
			[pscustomobject]@{ GuidSize = [System.BitConverter]::ToInt32($bytes, 4); HashStart = 36; HashEnd = 36 + $hashSize; BodyStart = $iconSizeOffset + 4 + $iconSize }
		}

		# テクスチャの本体(Texture.WriteAssetFile)の、最後の圧縮データ(長さ int32 と Deflate のバイト列)の位置。
		function Get-CompressedDataOffset([byte[]]$bytes, [int]$bodyStart)
		{
			$reader = [System.IO.BinaryReader]::new([System.IO.MemoryStream]::new($bytes))
			$reader.BaseStream.Position = $bodyStart + 8 # FileFormatTag, FileFormatVersion
			[void]$textureSettingsType.GetMethod("FromBinary", [type[]]@([System.IO.BinaryReader])).Invoke([Activator]::CreateInstance($textureSettingsType), @($reader))
			$reader.BaseStream.Position += 6 * 4 # Width, Height, ArraySize, Flags, MipLevels, Format
			[void]$reader.ReadString() # IBLPair の GUID
			return [int]$reader.BaseStream.Position
		}

		function Expand-Deflate([byte[]]$bytes, [int]$offset, [int]$length)
		{
			$output = [System.IO.MemoryStream]::new()
			$deflate = [System.IO.Compression.DeflateStream]::new([System.IO.MemoryStream]::new($bytes, $offset, $length), [System.IO.Compression.CompressionMode]::Decompress)
			$deflate.CopyTo($output)
			$deflate.Dispose()
			return , $output.ToArray()
		}

		function Test-SameBytes([byte[]]$a, [int]$aStart, [int]$aEnd, [byte[]]$b, [int]$bStart, [int]$bEnd)
		{
			if (($aEnd - $aStart) -ne ($bEnd - $bStart)) { return $false }
			for ($i = 0; $i -lt ($aEnd - $aStart); ++$i) { if ($a[$aStart + $i] -ne $b[$bStart + $i]) { return $false } }
			return $true
		}

		# 除外するもの:
		#   - GUID(オフセット8から16バイト)とインポート日時(オフセット24から8バイト): 書き出すたびに変わる。
		#   - テクスチャだけ: ハッシュ(圧縮データの SHA-256)と、圧縮データのバイト列そのもの(代わりに展開した内容を比べる)。
		#     このテストを動かす pwsh(.NET 10)の Deflate の出力が、DXForgeImport とエディタ(.NET 8)のものとバイト単位では異なるため。
		function Compare-AssetFiles([string]$name, [string]$cliFile, [string]$editorFile, [switch]$Texture)
		{
			$a = [System.IO.File]::ReadAllBytes($cliFile)
			$b = [System.IO.File]::ReadAllBytes($editorFile)
			$la = Get-AssetLayout $a
			$lb = Get-AssetLayout $b
			Check ($la.GuidSize -eq 16 -and $lb.GuidSize -eq 16) "比較(${name}): 除外するオフセット8-31が GUID(16バイト)とインポート日時(8バイト)の位置である"
			$same = (Test-SameBytes $a 0 8 $b 0 8) -and (Test-SameBytes $a 32 $la.HashStart $b 32 $lb.HashStart) -and (Test-SameBytes $a $la.HashEnd $la.BodyStart $b $lb.HashEnd $lb.BodyStart)
			Check $same "比較(${name}): ヘッダ(asset type、ハッシュの長さ、アイコン)が一致"
			if ($Texture)
			{
				$ca = Get-CompressedDataOffset $a $la.BodyStart
				$cb = Get-CompressedDataOffset $b $lb.BodyStart
				Check ((Test-SameBytes $a $la.BodyStart $ca $b $lb.BodyStart $cb)) "比較(${name}): 本体(形式のバージョン、インポート設定、寸法、フラグ、ミップ数、フォーマット、IBLの組)が一致"
				$ea = Expand-Deflate $a ($ca + 4) ([System.BitConverter]::ToInt32($a, $ca))
				$eb = Expand-Deflate $b ($cb + 4) ([System.BitConverter]::ToInt32($b, $cb))
				Check ($ea.Length -gt 0 -and (Test-SameBytes $ea 0 $ea.Length $eb 0 $eb.Length)) "比較(${name}): 圧縮データを展開した内容(全ミップの画素、$($ea.Length) バイト)が一致"
			}
			else
			{
				Check ((Test-SameBytes $a $la.HashStart $la.HashEnd $b $lb.HashStart $lb.HashEnd) -and (Test-SameBytes $a $la.BodyStart $a.Length $b $lb.BodyStart $b.Length)) `
					"比較(${name}): ハッシュと本体(インポート設定、LOD、メッシュ)がバイト単位で一致 (長さ $($a.Length) / $($b.Length))"
			}
		}

		# エディタではインポート設定の画面(TextureImportSettingsConfigurator.Import)がソース画像を設定してから ContentHelper.Import を呼ぶ。
		$settings = [Activator]::CreateInstance($textureSettingsType)
		$settings.Sources.Add($TextureFile)
		$asset = $editorImport.Invoke($null, @([string]$TextureFile, $settings, [string]$editorDir))
		Compare-AssetFiles "テクスチャ" $cliTexture (Join-Path $editorDir "checker64.asset") -Texture

		if ($cliGeometry)
		{
			$settings = [Activator]::CreateInstance($geometrySettingsType)
			$settings.SmoothingAngle = 90
			$asset = $editorImport.Invoke($null, @([string]$FbxFile, $settings, [string]$editorDir))
			Compare-AssetFiles "FBX" $cliGeometry (Join-Path $editorDir ([System.IO.Path]::GetFileName($cliGeometry)))
		}

		# 壊れたFBXをエディタの経路でインポートすると、ImportingItem が失敗になり、.asset を書かず、エラーログが出る(#110)
		$brokenEditorDir = Join-Path $WorkDir "editor_broken"
		New-Item -ItemType Directory -Path $brokenEditorDir | Out-Null
		# Logger.Log は画面のログ(Messages)へ Dispatcher 経由で積むので、Log() を呼んだスレッドで同期的に通知される MessageLogged で受け取る
		$messageLogged = $editor.GetType("DXForgeEditor.Utilities.Logger", $true).GetEvent("MessageLogged")
		$loggedErrors = [System.Collections.Generic.List[string]]::new()
		$onMessageLogged = [System.Management.Automation.LanguagePrimitives]::ConvertTo({ param($type, $message) if ("$type" -eq "Error") { $loggedErrors.Add($message) } }.GetNewClosure(), $messageLogged.EventHandlerType)
		$messageLogged.AddEventHandler($null, $onMessageLogged)
		try
		{
			$asset = $editorImport.Invoke($null, @([string]$brokenFbx, [Activator]::CreateInstance($geometrySettingsType), [string]$brokenEditorDir))
		}
		finally
		{
			$messageLogged.RemoveEventHandler($null, $onMessageLogged)
		}
		$importingItems = $editor.GetType("DXForgeEditor.Content.ImportingItemCollection", $true).GetProperty("ImportingItems", $flags).GetValue($null)
		$item = @($importingItems | Where-Object { [object]::ReferenceEquals($_.Asset, $asset) })
		$status = if ($item.Count -eq 1) { "$($item[0].Status)" } else { "(ImportingItem が $($item.Count) 個)" }
		Check ($status -eq "Failed") "壊れたFBX(エディタ): ImportingItem.Status が Failed (実際: $status)"
		Check (@(Get-ChildItem $brokenEditorDir -ErrorAction SilentlyContinue).Count -eq 0) "壊れたFBX(エディタ): .asset を書かない"
		Check (@($loggedErrors | Where-Object { $_ -like "*FBX*$brokenFbx*" }).Count -gt 0) "壊れたFBX(エディタ): FBX のインポートの失敗がエラーログに出る (実際: $($loggedErrors -join ' / '))"

		Check-NoAssert "エディタとの互換" $assertsBeforeEditor

		$environment.GetProperty("TempFolder").SetValue($null, $null)
		[void]$contentToolsApi.GetMethod("ShutDownContentTools").Invoke($null, @())
	}
}
finally
{
	Remove-Item -Recurse -Force $WorkDir -ErrorAction SilentlyContinue
}

Write-Host ""
if ($failures.Count -gt 0)
{
	Write-Host "失敗 $($failures.Count) 件"
	exit 1
}
Write-Host "すべて成功"
exit 0
