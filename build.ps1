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
    このスクリプトも非ゼロの終了コードを返す。

.PARAMETER WarningsAsErrors
    警告をエラーとして扱う。ネイティブ側は Directory.Build.props 経由で /WX を付与し、
    エディタ側は -p:TreatWarningsAsErrors=true を付けてフルビルドする(警告0件を維持するため)。
    ローカル開発を妨げないよう既定はオフで、CIから指定する想定。

.PARAMETER DetailedLog
    ビルドツールの出力を全文表示する。既定ではエラーと警告件数の要約だけを表示する
    (出力が非常に大きく、特にエディタ側は約132,000文字に達するため)。

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
		& $Command
		return $LASTEXITCODE
	}

	$output = (& $Command 2>&1 | Out-String)
	$exit = $LASTEXITCODE

	$errorLines = [regex]::Matches($output, '(?m)^.*\berror [A-Z]+[0-9]+.*$') |
		ForEach-Object { $_.Value.Trim() } | Select-Object -Unique
	foreach ($line in $errorLines) { Write-Host "  $line" }

	$warningCodes = [regex]::Matches($output, '\bwarning ([A-Z]+[0-9]+)') | ForEach-Object { $_.Groups[1].Value }
	if ($warningCodes.Count -gt 0)
	{
		$grouped = $warningCodes | Group-Object | Sort-Object Count -Descending
		$top = ($grouped | Select-Object -First 5 | ForEach-Object { "$($_.Name) x$($_.Count)" }) -join ", "
		$rest = if ($grouped.Count -gt 5) { " ほか$($grouped.Count - 5)種" } else { "" }
		Write-Host "  警告 $($warningCodes.Count)件: $top$rest"
	}
	else
	{
		Write-Host "  警告 0件"
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
	tests         = $null
	exitCode      = 0
}
$exitCode = 0

if ($Target -eq "native" -or $Target -eq "all")
{
	$msbuild = Resolve-MSBuildPath
	$msbuildArgs = @(
		"$RepoRoot\DXForge.sln",
		"/nologo",
		"/m",
		"/p:Configuration=$Configuration",
		"/p:Platform=$Platform"
	)
	if (-not $DetailedLog) { $msbuildArgs += "/v:minimal" }
	if ($Project) { $msbuildArgs += "/t:$Project" }
	if ($WarningsAsErrors) { $msbuildArgs += "/p:DXForgeWarningsAsErrors=true" }

	Write-Host "== Native build [$Configuration|$Platform]$(if ($Project) { " target=$Project" }) =="
	$nativeExit = Invoke-BuildStep -Command { & $msbuild @msbuildArgs } -ShowFullLog:$DetailedLog
	$summary.native = [ordered]@{ exitCode = $nativeExit; project = $Project; warningsAsErrors = [bool]$WarningsAsErrors }
	if ($nativeExit -ne 0) { $exitCode = $nativeExit }
}

if ($Target -eq "editor" -or $Target -eq "all")
{
	$editorConfiguration = Get-EditorConfiguration $Configuration
	$csproj = "$RepoRoot\DXForgeEditor\DXForgeEditor.csproj"

	# インクリメンタルビルドでは最新のファイルが再コンパイルされず警告も再出力されないため、
	# 警告をエラー化するときはフルビルドにして取りこぼしを防ぐ。
	$editorArgs = @()
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
	if (-not (Test-Path $testExe))
	{
		throw "$testExe が見つかりません。先に -Target native (または all) でビルドしてください。"
	}

	Write-Host "== Running tests: $testExe =="
	$testArgs = @()
	if ($Json) { $testArgs += "--json" }
	& $testExe @testArgs
	$testsExit = $LASTEXITCODE
	$summary.tests = [ordered]@{ exitCode = $testsExit; path = $testExe }
	if ($testsExit -ne 0 -and $exitCode -eq 0) { $exitCode = $testsExit }
}

if ($Json)
{
	$summary.exitCode = $exitCode
	($summary | ConvertTo-Json -Depth 4 -Compress)
}

exit $exitCode
