<#
.SYNOPSIS
    DXForgeEX をCLIからビルド・テスト実行するためのスクリプト。

.DESCRIPTION
    ネイティブ側(DXForgeEngine/EngineTest/ContentTools/EngineUnitTests)はvswhereで見つけた
    MSBuild.exeでDXForge.slnをビルドし、エディタ側(DXForgeEditor)はdotnet buildでビルドする。
    -RunTestsを指定すると、ビルド後にヘッドレスのEngineUnitTests.exeを実行し、その終了コードを
    このスクリプト全体の終了コードに反映する。-Jsonを指定すると、最後に1行のJSON要約を標準出力
    に出す(将来、AIエージェントやCIがこのスクリプトを呼び出す前提)。

    NOTE: ネイティブ側は必ず DXForge.sln 経由でビルドすること。.vcxprojを直接MSBuildに渡すと、
    $(SolutionDir)がプロジェクト自身のディレクトリにフォールバックしてしまい、インクルードパスが
    壊れてビルドに失敗する(MSBuildの既知の挙動)。

.PARAMETER Configuration
    Debug / DebugEditor / Release / ReleaseEditor のいずれか。既定値: DebugEditor。

.PARAMETER Target
    native / editor / all のいずれか。既定値: all。

.PARAMETER Project
    -Target native のときのみ有効。DXForge.sln内の特定プロジェクトのみビルドする
    (例: EngineUnitTests, DXForgeEngine, EngineTest, ContentTools)。省略時はソリューション全体。

.PARAMETER RunTests
    ネイティブビルド成功後にEngineUnitTests.exeを実行し、失敗したテストケースがあれば
    このスクリプトも非ゼロの終了コードを返す。EngineUnitTestsはDebug/Release構成にしか
    マッピングされていないため、*Editor構成や -Target editor、別の -Project を指定したときは、
    対応するDebug/Release構成でEngineUnitTestsを追加でビルドしてから実行する(以前のビルドで
    残った古い実行ファイルを黙って実行しないため。#60)。ビルドに失敗したときはテストを実行しない。

.PARAMETER WarningsAsErrors
    警告をエラーとして扱う。ネイティブ側は Directory.Build.props 経由で /WX を付与し、
    エディタ側は -p:TreatWarningsAsErrors=true を付けてフルビルドする(警告0件を維持するため)。
    ローカル開発を妨げないよう既定はオフで、CIから指定する想定。

.PARAMETER DetailedLog
    ビルドツールの出力を全文表示する。既定ではエラーと警告件数の要約だけを表示する
    (出力が非常に大きく、特にエディタ側は約132,000文字に達するため)。

.PARAMETER NoRestore
    ネイティブビルドの前に Restore.ps1 で packages/ (DirectXTex・DXC)とエディタのNuGet依存を
    復元する処理を省く。
    既に揃っているときの復元はファイルの有無を見るだけで、ネットワークにはアクセスしない。

.PARAMETER Json
    最後に1行のJSON要約を標準出力に出す。EngineUnitTests.exeにも--jsonを渡す。

.EXAMPLE
    ./build.ps1 -Target native -Project EngineUnitTests -RunTests
    EngineUnitTestsだけをビルドして実行する(最速のフィードバックループ)。

.EXAMPLE
    ./build.ps1 -Configuration Debug -RunTests -Json
    ネイティブ+エディタ両方をDebugでビルドし、テストを実行してJSON要約を出す。
#>
[CmdletBinding()]
param(
	[ValidateSet("Debug", "DebugEditor", "Release", "ReleaseEditor")]
	[string]$Configuration = "DebugEditor",

	[ValidateSet("native", "editor", "all")]
	[string]$Target = "all",

	[string]$Project,

	[switch]$RunTests,

	[switch]$WarningsAsErrors,

	[switch]$DetailedLog,

	[switch]$NoRestore,

	[switch]$Json
)

$ErrorActionPreference = "Stop"
$RepoRoot = $PSScriptRoot
$Platform = "x64"

# ビルドツールの出力をそのまま流すと非常に大きくなるため(実測: ネイティブ約7,400文字、
# エディタ約132,000文字)、既定では要約だけを表示する。エラーは原因特定に必要なので全文
# 表示し、警告はコード別の件数に畳む。完全なログが必要なときは -DetailedLog を指定する。
function Invoke-BuildStep
{
	param(
		[Parameter(Mandatory)][scriptblock]$Command,
		[switch]$ShowFullLog
	)

	if ($ShowFullLog)
	{
		# Out-Hostで画面に流す: そのままだとログが関数の戻り値に混ざって呼び出し側の変数に
		# 吸い込まれ、画面に出ないうえ終了コードも配列になってしまうため(#40)
		& $Command | Out-Host
		$exit = $LASTEXITCODE
		if ($exit -ne 0) { Write-Host "  失敗 (終了コード $exit)" }
		return $exit
	}

	$output = (& $Command 2>&1 | Out-String)
	$exit = $LASTEXITCODE

	# コードのないエラー行(NuGetのターゲットが出す「error : …」など)も拾う(#40)。
	# 「error」の直後に(コードと)コロンが続く形に限り、パス中の単語や「0 Error(s)」は拾わない。
	$errorLines = @([regex]::Matches($output, '(?m)^.*\berror(?: [A-Z]+[0-9]+)?\s*:.*$') |
		ForEach-Object { $_.Value.Trim() } | Select-Object -Unique)
	foreach ($line in $errorLines) { Write-Host "  $line" }

	# @()で包む: パイプラインの結果が1要素だと配列ではなく要素そのものが代入され、
	# GroupInfoのCount(そのグループの件数)を種類の数と取り違えるため(#15)
	$warningCodes = @([regex]::Matches($output, '\bwarning ([A-Z]+[0-9]+)') | ForEach-Object { $_.Groups[1].Value })
	if ($warningCodes.Count -gt 0)
	{
		$grouped = @($warningCodes | Group-Object | Sort-Object Count -Descending)
		$top = ($grouped | Select-Object -First 5 | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join ", "
		$rest = if ($grouped.Count -gt 5) { " ほか$($grouped.Count - 5)種" } else { "" }
		Write-Host "  警告 $($warningCodes.Count)件: $top$rest"
	}
	else
	{
		Write-Host "  警告 0件"
	}

	if ($exit -ne 0)
	{
		Write-Host "  失敗 (終了コード $exit)"
		# エラー行を1つも拾えなかったときは、手がかりとしてログの末尾を出す
		if ($errorLines.Count -eq 0)
		{
			Write-Host "  エラー行を抽出できませんでした。ログの末尾:"
			$output -split '\r?\n' | Where-Object { $_.Trim() } | Select-Object -Last 15 |
				ForEach-Object { Write-Host "    $_" }
		}
	}

	Write-Host "  (完全なログは -DetailedLog で表示できます)"
	return $exit
}

function Resolve-MSBuildPath
{
	# NOTE: ハードコードされたドライブパスに頼らず、環境変数とPATHの両方を見る
	#       (CIランナーなど Program Files の場所や構成が異なる環境でも動くようにするため)。
	$vswhereCandidates = @(
		(Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'),
		(Join-Path $env:ProgramFiles 'Microsoft Visual Studio\Installer\vswhere.exe'),
		(Get-Command vswhere.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source)
	) | Where-Object { $_ -and (Test-Path $_) }

	foreach ($vswhere in $vswhereCandidates)
	{
		$msbuild = & $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
		if ($msbuild) { return $msbuild }
	}

	# vswhere経由で見つからない場合は、PATH上のMSBuildを使う(setup-msbuildアクション等)
	$fromPath = Get-Command msbuild.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source
	if ($fromPath) { return $fromPath }

	throw 'MSBuild.exeが見つかりません。Visual Studio 2022以降(「C++によるデスクトップ開発」ワークロード)をインストールしてください。'
}

# EngineTest/EngineUnitTestsはDebugEditor/ReleaseEditorソリューション構成ではビルド対象外
# (Debug/Releaseにしかマッピングされていない)。実際の出力先を知るにはこの対応関係が必要。
function Get-NativeOutputConfiguration([string]$Config)
{
	if ($Config -like "*Editor") { return $Config.Substring(0, $Config.Length - "Editor".Length) }
	return $Config
}

# ネイティブ側のMSBuild呼び出し。ソリューションのビルドと、-RunTestsのためのEngineUnitTestsの
# 追加ビルド(#60)で共有する。
function Invoke-NativeBuild([string]$MSBuild, [string]$Config, [string]$TargetProject)
{
	$msbuildArgs = @(
		"$RepoRoot\DXForge.sln",
		"/nologo",
		"/m",
		"/p:Configuration=$Config",
		"/p:Platform=$Platform"
	)
	if (-not $DetailedLog) { $msbuildArgs += "/v:minimal" }
	if ($TargetProject) { $msbuildArgs += "/t:$TargetProject" }
	if ($WarningsAsErrors) { $msbuildArgs += "/p:DXForgeWarningsAsErrors=true" }

	Write-Host "== Native build [$Config|$Platform]$(if ($TargetProject) { " target=$TargetProject" }) =="
	return Invoke-BuildStep -Command { & $MSBuild @msbuildArgs } -ShowFullLog:$DetailedLog
}

function Get-EditorConfiguration([string]$Config)
{
	if ($Config -like "*Editor") { return $Config }
	return "${Config}Editor"
}

$summary = [ordered]@{
	configuration = $Configuration
	target        = $Target
	native        = $null
	editor        = $null
	testsBuild    = $null
	tests         = $null
	exitCode      = 0
}
$exitCode = 0

if ($Target -eq "native" -or $Target -eq "all")
{
	$msbuild = Resolve-MSBuildPath
	if (-not $NoRestore)
	{
		Write-Host "== Restore packages [$Configuration] =="
		& "$RepoRoot\Restore.ps1" -Configuration $Configuration -Project $Project -MSBuildPath $msbuild
	}
	$nativeExit = Invoke-NativeBuild $msbuild $Configuration $Project
	$summary.native = [ordered]@{ exitCode = $nativeExit; project = $Project; warningsAsErrors = [bool]$WarningsAsErrors }
	if ($nativeExit -ne 0) { $exitCode = $nativeExit }
}

if ($Target -eq "editor" -or $Target -eq "all")
{
	$editorConfiguration = Get-EditorConfiguration $Configuration
	$csproj = "$RepoRoot\DXForgeEditor\DXForgeEditor.csproj"

	# Platformを渡さないとAnyCPU扱いになり、出力先がリポジトリ直下の<Config>\になってしまう。
	# VSのソリューションビルドと同じx64\<Config>\(エンジンのDLLと同じフォルダ)に出力させる。
	$editorArgs = @("-p:Platform=$Platform")
	# インクリメンタルビルドでは最新のファイルが再コンパイルされず警告も再出力されないため、
	# 警告をエラー化するときはフルビルドにして取りこぼしを防ぐ。
	if ($WarningsAsErrors) { $editorArgs += @("-p:TreatWarningsAsErrors=true", "--no-incremental") }

	Write-Host "== Editor build [$editorConfiguration|$Platform] =="
	$editorExit = Invoke-BuildStep -Command { & dotnet build $csproj -c $editorConfiguration --nologo -v:minimal @editorArgs } -ShowFullLog:$DetailedLog
	$summary.editor = [ordered]@{ exitCode = $editorExit; configuration = $editorConfiguration; warningsAsErrors = [bool]$WarningsAsErrors }
	if ($editorExit -ne 0 -and $exitCode -eq 0) { $exitCode = $editorExit }
}

if ($RunTests)
{
	$nativeOutputConfiguration = Get-NativeOutputConfiguration $Configuration
	$testExe = "$RepoRoot\$Platform\$nativeOutputConfiguration\EngineUnitTests.exe"

	# 上のネイティブのビルドがEngineUnitTestsを今のソースからビルドしたかどうか。していなければ
	# (*Editor構成・-Target editor・別の -Project)、出力先に残っている実行ファイルは古い可能性が
	# あるため、Debug/Release構成でEngineUnitTestsだけを追加でビルドする(#60)。
	$testsBuiltByNative = ($Target -ne "editor") -and ($Configuration -eq $nativeOutputConfiguration) -and
		(-not $Project -or $Project -eq "EngineUnitTests")
	if ($testsBuiltByNative)
	{
		$testsBuildExit = $nativeExit
	}
	else
	{
		if (-not $msbuild) { $msbuild = Resolve-MSBuildPath }
		# EngineUnitTestsはpackages/の中身を使わないため、復元は不要
		$testsBuildExit = Invoke-NativeBuild $msbuild $nativeOutputConfiguration "EngineUnitTests"
		$summary.testsBuild = [ordered]@{ exitCode = $testsBuildExit; configuration = $nativeOutputConfiguration }
		if ($testsBuildExit -ne 0 -and $exitCode -eq 0) { $exitCode = $testsBuildExit }
	}

	if ($testsBuildExit -ne 0)
	{
		# ビルドに失敗しても以前の実行ファイルは残っているため、それを実行すると古い結果を返してしまう
		Write-Host "== Tests skipped: EngineUnitTests のビルドに失敗したため実行しません =="
		$summary.tests = [ordered]@{ exitCode = $null; path = $testExe; skipped = $true }
	}
	else
	{
		Write-Host "== Running tests: $testExe =="
		$testArgs = @()
		if ($Json) { $testArgs += "--json" }
		& $testExe @testArgs
		$testsExit = $LASTEXITCODE
		$summary.tests = [ordered]@{ exitCode = $testsExit; path = $testExe }
		if ($testsExit -ne 0 -and $exitCode -eq 0) { $exitCode = $testsExit }
	}
}

if ($Json)
{
	$summary.exitCode = $exitCode
	($summary | ConvertTo-Json -Depth 4 -Compress)
}

exit $exitCode
