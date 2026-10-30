<#
.SYNOPSIS
    Create Project の Path 欄の既定値の決め方(GameProject.ProjectsPath)を確かめる回帰テスト(Issue #141)。

.DESCRIPTION
    ビルド済みの DXForgeEditor.dll を読み込み、エディタと同じコード(ProjectsPath.Resolve)をリフレクションで呼ぶ。
    エディタのウィンドウは開かず、エンジンのDLLも読み込まないので、GUIもGPUも不要。
    ユーザー環境変数 DXFORGE_PROJECTS は読みも書きもしない(値とユーザーフォルダは引数として渡す)。

    確かめること:
      1. DXFORGE_PROJECTS が絶対パスなら、それ(末尾は \)を既定にする。末尾の \ の有無で結果は変わらない。
         フォルダがまだなくても使う(作成時に作られる)。
      2. 未設定・空・空白だけなら、<ユーザーフォルダ>\DXForgeProjects\ を既定にし、Warning は出さない。
      3. 使えない値(相対パス、無効な文字、ドライブなし、存在しないドライブ)なら 2. の既定にし、
         値と理由を含む Warning を返す。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。x64\<Configuration>\ の DXForgeEditor.dll を使う。

.EXAMPLE
    pwsh -NoProfile -File DXForgeEditor/Tests/projects_path_test.ps1
#>
[CmdletBinding()]
param(
	[ValidateSet("DebugEditor", "ReleaseEditor")]
	[string]$Configuration = "DebugEditor"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$OutDir = Join-Path $RepoRoot "x64\$Configuration"
$failures = [System.Collections.Generic.List[string]]::new()

function Check([bool]$condition, [string]$message)
{
	Write-Host ($(if ($condition) { "  PASS " } else { "  FAIL " }) + $message)
	if (-not $condition) { $failures.Add($message) }
}

$editor = [System.Reflection.Assembly]::LoadFrom((Join-Path $OutDir "DXForgeEditor.dll"))
$flags = [System.Reflection.BindingFlags]"Public,NonPublic,Static"
$projectsPath = $editor.GetType("DXForgeEditor.GameProject.ProjectsPath", $true)
$resolve = $projectsPath.GetMethod("Resolve", $flags)

function Resolve-ProjectsPath($environmentPath, [string]$userProfile)
{
	# [string] にすると $null が "" になるので、未設定(null)はそのまま渡す
	$env = if ($null -ne $environmentPath) { [string]$environmentPath } else { $null }
	$resolve.Invoke($null, @($env, $userProfile))
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("dxforge_projects_path_test_" + [Guid]::NewGuid().ToString("N"))
try
{
	$profileDir = Join-Path $work "profile"
	$null = New-Item -ItemType Directory -Force $profileDir
	$defaultPath = Join-Path $profileDir "DXForgeProjects\"

	Write-Host "1. DXFORGE_PROJECTS is an absolute path"
	# このマシンの設定値と同じ形。F: がないマシン(CI)でも結果は同じになるように、ドライブの有無で分ける
	$fDrive = Test-Path "F:\"
	$r = Resolve-ProjectsPath "F:\DXForgeProjects" $profileDir
	if ($fDrive)
	{
		Check ($r.Path -eq "F:\DXForgeProjects\" -and $null -eq $r.Warning) "F:\DXForgeProjects -> $($r.Path)"
		$r = Resolve-ProjectsPath "F:\DXForgeProjects\" $profileDir
		Check ($r.Path -eq "F:\DXForgeProjects\" -and $null -eq $r.Warning) "F:\DXForgeProjects\ (trailing \) -> $($r.Path)"
	}
	else
	{
		Check ($r.Path -eq $defaultPath -and $r.Warning) "F:\DXForgeProjects on a machine without F: falls back with a warning: $($r.Warning)"
	}
	$existing = Join-Path $work "projects"
	$null = New-Item -ItemType Directory -Force $existing
	$r = Resolve-ProjectsPath $existing $profileDir
	Check ($r.Path -eq ($existing + "\") -and $null -eq $r.Warning) "an existing folder (no trailing \) -> $($r.Path)"
	$r = Resolve-ProjectsPath ($existing + "\") $profileDir
	Check ($r.Path -eq ($existing + "\") -and $null -eq $r.Warning) "an existing folder (trailing \) -> $($r.Path)"
	$r = Resolve-ProjectsPath ("  " + $existing + "  ") $profileDir
	Check ($r.Path -eq ($existing + "\") -and $null -eq $r.Warning) "surrounding spaces are trimmed -> '$($r.Path)'"
	$missing = Join-Path $work "not\yet\created"
	$r = Resolve-ProjectsPath $missing $profileDir
	Check ($r.Path -eq ($missing + "\") -and $null -eq $r.Warning) "a folder that does not exist yet is still used -> $($r.Path)"

	Write-Host "2. DXFORGE_PROJECTS is not set"
	$r = Resolve-ProjectsPath $null $profileDir
	Check ($r.Path -eq $defaultPath -and $null -eq $r.Warning) "unset -> $($r.Path)"
	$r = Resolve-ProjectsPath "" $profileDir
	Check ($r.Path -eq $defaultPath -and $null -eq $r.Warning) "empty -> $($r.Path)"
	$r = Resolve-ProjectsPath "   " $profileDir
	Check ($r.Path -eq $defaultPath -and $null -eq $r.Warning) "whitespace only -> $($r.Path)"
	$r = Resolve-ProjectsPath $null ($profileDir + "\")
	Check ($r.Path -eq $defaultPath) "a user folder with a trailing \ gives the same default -> $($r.Path)"
	# エディタが渡すのと同じ実際のユーザーフォルダ
	$userProfile = [Environment]::GetFolderPath([Environment+SpecialFolder]::UserProfile)
	$r = Resolve-ProjectsPath $null $userProfile
	Check ($r.Path -eq (Join-Path $userProfile "DXForgeProjects\")) "with the real user folder -> $($r.Path)"
	$documents = [Environment]::GetFolderPath([Environment+SpecialFolder]::MyDocuments)
	Check (-not $r.Path.StartsWith($documents + "\", [StringComparison]::OrdinalIgnoreCase)) "the default is not under Documents ($documents)"

	Write-Host "3. DXFORGE_PROJECTS is unusable"
	# 使われていないドライブ文字を探す
	$unusedDrive = [char[]]([char]'Z'..[char]'D') | Where-Object { -not (Test-Path "$($_):\") } | Select-Object -First 1
	$invalid = @(
		@{ Value = "DXForgeProjects"; Reason = "relative path" },
		@{ Value = "..\DXForgeProjects"; Reason = "relative path with .." },
		@{ Value = "\DXForgeProjects"; Reason = "no drive (root-relative)" },
		@{ Value = "F:DXForgeProjects"; Reason = "drive-relative" },
		@{ Value = "F:\DXForge`0Projects"; Reason = "invalid character (NUL)" },
		@{ Value = "F:\DXForge|Projects"; Reason = "invalid character (|)" }
	)
	if ($unusedDrive) { $invalid += @{ Value = "$($unusedDrive):\DXForgeProjects"; Reason = "drive $($unusedDrive): does not exist" } }
	foreach ($case in $invalid)
	{
		$r = Resolve-ProjectsPath $case.Value $profileDir
		$shown = $case.Value.Replace("`0", "\0")
		Check ($r.Path -eq $defaultPath -and $r.Warning -and $r.Warning.Contains("DXFORGE_PROJECTS") -and $r.Warning.Contains($defaultPath)) "$($case.Reason) '$shown' -> $($r.Path) / $($r.Warning)"
	}
	$r = Resolve-ProjectsPath "DXForgeProjects" $profileDir
	Check ($r.Warning.Contains("DXForgeProjects")) "the warning shows the rejected value"
}
finally
{
	Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}

Write-Host ""
if ($failures.Count -gt 0)
{
	Write-Host "FAILED ($($failures.Count))"
	exit 1
}
Write-Host "ALL PASSED"
exit 0
