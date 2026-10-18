<#
.SYNOPSIS
    リポジトリに含めていない packages/ (DirectXTexのNuGetパッケージ・DXC)を復元するスクリプト。

.DESCRIPTION
    packages/ は .gitignore で除外しているため、新しいclone/worktreeでは空になっている。
    このスクリプトは足りないものだけを取得する。既に揃っていれば、ファイルの有無を見るだけで
    ネットワークにはアクセスせずに終わる。build.ps1 がネイティブのビルドの前に呼び出す
    (build.ps1 -NoRestore で止められる)。単体でも実行できる。

    - DirectXTex: 各プロジェクトの packages.config に書かれたパッケージ(packages/<id>.<version>)が
      無ければ、MSBuild DXForge.sln -t:restore -p:RestorePackagesConfig=true で復元する
      (nuget.exe は不要)。packages.config を使うのは ContentTools だけで、ContentTools は
      *Editor 構成にしかマッピングされていないため、Debug/Release 構成では何もしない
      (MSBuildもその構成では復元対象が無いと判断する)。
    - DXC: packages/DirectXShaderCompiler が空なら GetDXC.ps1 で最新版を取得する
      (GitHub APIを使うため、既にあるときは呼ばない)。

.PARAMETER Configuration
    Debug / DebugEditor / Release / ReleaseEditor のいずれか。既定値: DebugEditor。

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

$started = Get-Date
$restored = $false

if ($Configuration -like "*Editor")
{
	$missing = @(Get-MissingNuGetPackages)
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
if (-not (Test-Path (Join-Path $dxcDir "*")))
{
	Write-Host "  DXCを取得します: $dxcDir"
	& (Join-Path $RepoRoot "GetDXC.ps1") $dxcDir | Out-Null
	if ($LASTEXITCODE -ne 0) { throw "DXCの取得に失敗しました(終了コード $LASTEXITCODE)。" }
	$restored = $true
}

$elapsed = [int]((Get-Date) - $started).TotalMilliseconds
if ($restored) { Write-Host "  復元しました($elapsed ms)" }
else { Write-Host "  packages/ は揃っています。復元は不要です($elapsed ms)" }
