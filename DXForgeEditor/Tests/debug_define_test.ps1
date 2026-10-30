<#
.SYNOPSIS
    エディタ(DXForgeEditor)とインポーター(DXForgeImport)の構成ごとに、C# の DEBUG が定義されているかを確かめる回帰テスト(Issue #109)。

.DESCRIPTION
    .NET SDK が DEBUG を自動で定義するのは構成の名前が Debug のときだけなので、DebugEditor では .csproj で定義する必要がある。
    それがないと Debug.Assert / Debug.WriteLine が([Conditional("DEBUG")] のため)すべてコンパイルから外れる。

    確かめること:
      1. 評価したプロパティ(dotnet msbuild -getProperty): 両方のプロジェクトで、DebugEditor は DEBUG と TRACE を定義して最適化なし、
         ReleaseEditor は DEBUG なし・TRACE ありで最適化あり。ビルドは要らない。
      2. ビルド済みの DXForgeEditor.dll(x64\<構成>\): DebugEditor には System.Diagnostics.Debug.Assert の呼び出しが残っていて、
         ReleaseEditor には残っていない。メタデータを読むだけで、アセンブリは読み込まない(実行しない)。
         -Configuration で指定した構成の DLL は必須で、もう一方の構成の DLL はあれば確かめる。

.PARAMETER Configuration
    DebugEditor(既定)か ReleaseEditor。x64\<Configuration>\DXForgeEditor.dll がビルド済みであること。

.EXAMPLE
    pwsh -NoProfile -File DXForgeEditor/Tests/debug_define_test.ps1 -Configuration DebugEditor
#>
[CmdletBinding()]
param(
	[ValidateSet("DebugEditor", "ReleaseEditor")]
	[string]$Configuration = "DebugEditor"
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$failures = [System.Collections.Generic.List[string]]::new()

function Check([bool]$condition, [string]$message)
{
	Write-Host ($(if ($condition) { "  PASS " } else { "  FAIL " }) + $message)
	if (-not $condition) { $failures.Add($message) }
}

function Get-EvaluatedProperties([string]$project, [string]$config)
{
	$output = & dotnet msbuild $project -nologo "-p:Configuration=$config" "-p:Platform=x64" -getProperty:DefineConstants -getProperty:Optimize
	if ($LASTEXITCODE -ne 0) { throw "dotnet msbuild -getProperty failed for $project ($config): $output" }
	($output -join "`n" | ConvertFrom-Json).Properties
}

# System.Diagnostics.Debug の Name を呼ぶ MemberReference の数を数える
function Get-DebugCallCount([string]$assemblyPath, [string]$name)
{
	$stream = [System.IO.File]::OpenRead($assemblyPath)
	try
	{
		$pe = [System.Reflection.PortableExecutable.PEReader]::new($stream)
		$reader = [System.Reflection.Metadata.PEReaderExtensions]::GetMetadataReader($pe)
		$count = 0
		foreach ($handle in $reader.MemberReferences)
		{
			$member = $reader.GetMemberReference($handle)
			if ($member.Parent.Kind -ne [System.Reflection.Metadata.HandleKind]::TypeReference) { continue }
			$type = $reader.GetTypeReference([System.Reflection.Metadata.TypeReferenceHandle]$member.Parent)
			if ($reader.GetString($type.Namespace) -eq "System.Diagnostics" -and $reader.GetString($type.Name) -eq "Debug" -and
				$reader.GetString($member.Name) -eq $name) { $count++ }
		}
		$pe.Dispose()
		$count
	}
	finally
	{
		$stream.Dispose()
	}
}

Write-Host "1. Evaluated DefineConstants / Optimize"
foreach ($project in @("DXForgeEditor\DXForgeEditor.csproj", "DXForgeImport\DXForgeImport.csproj"))
{
	foreach ($config in @("DebugEditor", "ReleaseEditor"))
	{
		$props = Get-EvaluatedProperties (Join-Path $RepoRoot $project) $config
		$defines = @($props.DefineConstants -split ";" | Where-Object { $_ })
		$isDebug = $config -eq "DebugEditor"
		Check (($defines -contains "DEBUG") -eq $isDebug) "$project [$config] DEBUG $(if ($isDebug) { 'is' } else { 'is not' }) defined: '$($props.DefineConstants)'"
		Check ($defines -contains "TRACE") "$project [$config] TRACE is defined: '$($props.DefineConstants)'"
		Check ($props.Optimize -eq "$(-not $isDebug)") "$project [$config] Optimize=$(-not $isDebug): '$($props.Optimize)'"
	}
}

Write-Host "2. Debug.Assert calls in the built DXForgeEditor.dll"
foreach ($config in @("DebugEditor", "ReleaseEditor"))
{
	$dll = Join-Path $RepoRoot "x64\$config\DXForgeEditor.dll"
	if (-not (Test-Path $dll))
	{
		if ($config -eq $Configuration) { Check $false "$dll exists (build it with ./build.ps1 -Target editor -Configuration $config)" }
		else { Write-Host "  SKIP $dll is not built" }
		continue
	}
	$asserts = Get-DebugCallCount $dll "Assert"
	if ($config -eq "DebugEditor") { Check ($asserts -gt 0) "[$config] Debug.Assert is compiled in: $asserts member reference(s)" }
	else { Check ($asserts -eq 0) "[$config] Debug.Assert is compiled out: $asserts member reference(s)" }
}

if ($failures.Count -gt 0)
{
	Write-Host "FAILED: $($failures.Count) check(s)"
	exit 1
}
Write-Host "All checks passed"
exit 0
