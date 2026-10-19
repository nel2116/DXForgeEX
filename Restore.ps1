<#
.SYNOPSIS
    リポジトリに含めていない packages/ (DirectXTexのNuGetパッケージ・DXC)を復元するスクリプト。

.DESCRIPTION
    packages/ は .gitignore で除外しているため、新しいclone/worktreeでは空になっている。
    このスクリプトは足りないものだけを取得する。既に揃っていれば、ファイルの有無を見るだけで
    ネットワークにはアクセスせずに終わる。build.ps1 がネイティブのビルドの前に呼び出す
    (build.ps1 -NoRestore で止められる)。単体でも実行できる。

    - NuGet(DirectXTex・エディタ): 各プロジェクトの packages.config に書かれたパッケージ
      (packages/<id>.<version>)か、DXForgeEditor/obj/project.assets.json が無ければ、
      MSBuild DXForge.sln -t:restore -p:RestorePackagesConfig=true で復元する(nuget.exe は不要)。
      この1回で packages.config(ContentTools の DirectXTex)と PackageReference(DXForgeEditor)の
      両方が復元される。どちらも *Editor 構成にしかマッピングされていないため、Debug/Release
      構成では何もしない。ネイティブのビルドは /restore なしで DXForge.sln を呼ぶため、*Editor
      構成で project.assets.json が無いと(obj だけを消したときなど)、ソリューションに含まれる
      DXForgeEditor が NETSDK1004 で失敗する(#61)。
    - DXC: packages/DirectXShaderCompiler が空なら GetDXC.ps1 で最新版を取得する
      (GitHub APIを使うため、既にあるときは呼ばない)。DXCを使うのは EngineTest
      (シェーダーのコンパイル)だけで、EngineTest は Debug/Release 構成にしかマッピングされて
      いないため、*Editor 構成や EngineTest を含まない -Project のときは取得しない(#61)。
      オフラインやGitHub APIのレート制限の下でも、EngineUnitTests などはビルドできる。

.PARAMETER Configuration
    Debug / DebugEditor / Release / ReleaseEditor のいずれか。既定値: DebugEditor。

.PARAMETER Project
    build.ps1 の -Project と同じ。ビルドするプロジェクト(MSBuildの /t: に渡す名前。; 区切りで
    複数可)。省略時はソリューション全体とみなす。DXCが必要かどうかの判断に使う。

.PARAMETER MSBuildPath
    復元に使う MSBuild.exe。省略時は vswhere で探す(build.ps1 からは解決済みのパスを渡す)。

.EXAMPLE
    ./Restore.ps1
    DebugEditor 構成に必要なパッケージを復元する。
#>
[CmdletBinding()]
param(
	[ValidateSet("Debug", "DebugEditor", "Release", "ReleaseEditor")]
	[string]$Configuration = "DebugEditor",

	[string]$Project,

	[string]$MSBuildPath
)

$ErrorActionPreference = "Stop"
$RepoRoot = $PSScriptRoot
$PackagesDir = Join-Path $RepoRoot "packages"
$Platform = "x64"

function Get-MissingNuGetPackages
{
	# プロジェクトはすべてリポジトリ直下の1階層目にあるため、再帰的には探さない
	# (packages/ や出力フォルダの中まで走査すると遅くなるため)
	$configs = Get-ChildItem $RepoRoot -Directory |
		Where-Object { $_.Name -ne "packages" } |
		ForEach-Object { Join-Path $_.FullName "packages.config" } |
		Where-Object { Test-Path $_ }

	foreach ($config in $configs)
	{
		foreach ($package in ([xml](Get-Content $config -Raw)).packages.package)
		{
			$name = "$($package.id).$($package.version)"
			if (-not (Test-Path (Join-Path $PackagesDir $name))) { $name }
		}
	}
}

# DXCを使うプロジェクト(EngineTest)が、この構成と -Project でビルドされるかどうか
function Test-DxcRequired
{
	if ($Configuration -like "*Editor") { return $false }
	if (-not $Project) { return $true }
	# /t: には「EngineTest:Rebuild」のようにターゲット名が付くこともあるため、その前だけを見る
	$projects = $Project -split ';' | ForEach-Object { ($_ -split ':')[0].Trim() }
	return $projects -contains "EngineTest"
}

$started = Get-Date
$restored = $false

if ($Configuration -like "*Editor")
{
	$missing = @(Get-MissingNuGetPackages)
	if (-not (Test-Path (Join-Path $RepoRoot "DXForgeEditor\obj\project.assets.json"))) { $missing += "DXForgeEditor(project.assets.json)" }
	if ($missing.Count -gt 0)
	{
		if (-not $MSBuildPath)
		{
			$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
			if (Test-Path $vswhere)
			{
				$MSBuildPath = & $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
			}
			if (-not $MSBuildPath) { $MSBuildPath = Get-Command msbuild.exe -ErrorAction SilentlyContinue | Select-Object -First 1 -ExpandProperty Source }
			if (-not $MSBuildPath) { throw 'MSBuild.exeが見つかりません。Visual Studio 2022以降をインストールしてください。' }
		}

		Write-Host "  NuGetパッケージを復元します: $($missing -join ', ')"
		$output = (& $MSBuildPath "$RepoRoot\DXForge.sln" /nologo /t:restore /p:RestorePackagesConfig=true "/p:Configuration=$Configuration" "/p:Platform=$Platform" /v:minimal 2>&1 | Out-String)
		if ($LASTEXITCODE -ne 0)
		{
			Write-Host $output
			throw "NuGetパッケージの復元に失敗しました(終了コード $LASTEXITCODE)。"
		}
		$restored = $true
	}
}

$dxcDir = Join-Path $PackagesDir "DirectXShaderCompiler"
if ((Test-DxcRequired) -and -not (Test-Path (Join-Path $dxcDir "*")))
{
	Write-Host "  DXCを取得します: $dxcDir"
	& (Join-Path $RepoRoot "GetDXC.ps1") $dxcDir | Out-Null
	if ($LASTEXITCODE -ne 0) { throw "DXCの取得に失敗しました(終了コード $LASTEXITCODE)。" }
	$restored = $true
}

$elapsed = [int]((Get-Date) - $started).TotalMilliseconds
if ($restored) { Write-Host "  復元しました($elapsed ms)" }
else { Write-Host "  必要なパッケージは揃っています。復元は不要です($elapsed ms)" }
