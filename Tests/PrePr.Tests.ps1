<#
.SYNOPSIS
	PrePr.ps1 の対応表、自己点検の項目、スタンプの回帰テスト(#195)。ビルドは実行しない。

.DESCRIPTION
	前半は -PlanOnly -Files で計画(JSON)を確かめる。後半は %TEMP% の使い捨てのリポジトリにスクリプトと
	偽の Tests/check_conventions.py をコピーして実行し、スタンプの有無を確かめる。

.PARAMETER ScriptPath
	テストするスクリプト。既定はリポジトリの PrePr.ps1。

.EXAMPLE
	pwsh -NoProfile -File Tests/PrePr.Tests.ps1
#>
[CmdletBinding()]
param(
	[string]$ScriptPath = (Join-Path $PSScriptRoot "..\PrePr.ps1")
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ScriptPath = (Resolve-Path $ScriptPath).Path

$failures = [System.Collections.Generic.List[string]]::new()
function Check([bool]$condition, [string]$message) {
	if ($condition) { Write-Host "  ok: $message" }
	else {
		Write-Host "  NG: $message" -ForegroundColor Red
		$failures.Add($message)
	}
}

# 本体はビルドを始めてしまうので、関数の定義だけを読み込む
$tokens = $null
$parseErrors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile($ScriptPath, [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count -gt 0) { throw "PrePr.ps1 の構文エラー: $($parseErrors[0].Message)" }
$wanted = @("Get-PrePrPlan", "Get-PrePrSteps", "Get-PrePrChecklist")
$functions = @($ast.FindAll({ param($node) $node -is [System.Management.Automation.Language.FunctionDefinitionAst] }, $false) |
	Where-Object { $wanted -contains $_.Name })
foreach ($name in $wanted) {
	if (-not ($functions | Where-Object Name -eq $name)) { throw "PrePr.ps1 に関数 $name がありません" }
}
foreach ($function in $functions) { . ([scriptblock]::Create($function.Extent.Text)) }

function Get-Plan([string[]]$Files, [string]$AddedText = "") {
	$plan = Get-PrePrPlan $Files
	[pscustomobject]@{ steps = @(Get-PrePrSteps $plan); unclassified = @($plan.Unclassified); checklist = @(Get-PrePrChecklist $Files $AddedText) }
}

$conventions = "python Tests/check_conventions.py"
$native = "./build.ps1 -Target native -Configuration Release -RunTests -WarningsAsErrors"
$editor = "./build.ps1 -Target editor -WarningsAsErrors"
$dll = "./build.ps1 -Target native -Configuration DebugEditor -Project DXForgeEngineDLL -WarningsAsErrors"
$tools = "./build.ps1 -Target native -Configuration DebugEditor -Project ContentTools -WarningsAsErrors"
$anyBuild = "./build.ps1"

Write-Host "== 対応表 =="
# ファイル, 含むコマンド(部分一致), 含まないコマンド(部分一致)
$cases = @(
	@("docs/dev/A.md", @(), @($anyBuild)),
	@("README.md", @(), @($anyBuild)),
	@(".claude/skills/ship-pr/SKILL.md", @(), @($anyBuild)),
	@(".claude/settings.json", @(), @($anyBuild)),
	@(".github/workflows/ci.yml", @(), @($anyBuild)),
	@("DXForgeEngine/Core/A.cpp", @($native), @($editor, $dll)),
	@("DXForgeEngine/Graphics/Shaders/A.hlsl", @($native), @($editor)),
	@("EngineUnitTests/Tests_A.cpp", @($native), @($editor)),
	@("EngineUnitTests/Tests/headless_run_test.py", @($native), @("python EngineUnitTests")),
	@("EngineTest/A.h", @($native), @($editor)),
	# editor_shader_compilation_test はエディタのラッパーを通すので、エディタのビルドも要る(PR #196 の Codex の指摘)
	@("DXForgeEngineDLL/A.cpp", @($native, $dll, $editor, "shader_compilation_test.py", "editor_shader_compilation_test.ps1"), @()),
	@("DXForgeEngineDLL/Tests/editor_shader_compilation_test.ps1", @($dll, $editor, "editor_shader_compilation_test.ps1"), @($native)),
	# ビルドする DebugEditor と同じ構成でテストする(primitive_mesh_cube_test の既定は ReleaseEditor。PR #196 の Codex の指摘)
	@("ContentTools/A.cpp", @($tools, "missing_source_import_test.py --config DebugEditor", "primitive_mesh_cube_test.py --config DebugEditor"), @($editor, $native)),
	@("ContentTools/FbxSdk.props", @($tools), @($editor, $native)),
	@("ContentTools/Tests/envmap_cpu_gpu_test.py", @($tools, "envmap_cpu_gpu_test.py --config DebugEditor"), @($native)),
	@("DXForgeImport/Tests/import_cli_test.ps1", @($editor, "import_cli_test.ps1 -ArgumentsOnly"), @($native)),
	@("DXForgeEditor/GameProject/A.cs", @($editor, "project_layout_view_test.ps1", "projects_path_test.ps1", "load_project_cli_test.ps1"), @($native)),
	@("DXForgeEditor/Content/A.cs", @($editor, "material_asset_test.ps1", "default_geometry_test.ps1"), @($native)),
	@("DXForgeEditor/Components/A.cs", @($editor, "material_asset_test.ps1"), @($native)),
	@("DXForgeEditor/Automation/A.cs", @($editor, "ops_cli_test.ps1"), @($native)),
	@("DXForgeEditor/Editors/A.xaml", @($editor, "project_layout_view_test.ps1"), @($native)),
	@("DXForgeEditor/DllWrappers/A.cs", @($editor, $dll, "engine_init_test.ps1", "editor_shader_compilation_test.ps1"), @($native)),
	@("DXForgeEditor/GameDev/EnginePath.cs", @($editor, "engine_path_test.ps1", "project_layout_view_test.ps1"), @($native)),
	@("DXForgeEditor/Utilities/A.cs", @($editor, "project_layout_view_test.ps1"), @($native)),
	@("DXForgeEditor/MainWindow.xaml.cs", @($editor, "project_layout_view_test.ps1"), @($native)),
	@("DXForgeEditor/DXForgeEditor.csproj", @($editor, "debug_define_test.ps1"), @($native)),
	@("DXForgeEditor/Resources/A.hlsl", @($editor), @($native)),
	@("DXForgeCli/Program.cs", @($editor, "load_project_cli_test.ps1", "ops_cli_test.ps1"), @($native)),
	@("DXForgeImport/Program.cs", @($editor, "import_cli_test.ps1 -ArgumentsOnly"), @($native)),
	@("DXForgeEditor/Tests/engine_path_test.ps1", @($editor, "engine_path_test.ps1"), @($native)),
	@("DXForgeCli/Tests/ops_cli_test.ps1", @($editor, "ops_cli_test.ps1"), @($native)),
	@("DXForgeEngineDLL/Tests/shader_compilation_test.py", @($dll, "python DXForgeEngineDLL/Tests/shader_compilation_test.py"), @($native)),
	@("ContentTools/Tests/primitive_mesh_cube_test.py", @($tools, "python ContentTools/Tests/primitive_mesh_cube_test.py --config DebugEditor"), @($native)),
	@("Tests/check_conventions.py", @("python Tests/check_conventions_test.py"), @($anyBuild)),
	@("Tests/guard_review_flow.py", @("python Tests/guard_review_flow_test.py"), @($anyBuild)),
	@("Tests/guard_review_flow_test.py", @("python Tests/guard_review_flow_test.py"), @($anyBuild)),
	@("Tests/CleanupMerged.Tests.ps1", @("Tests/CleanupMerged.Tests.ps1"), @($anyBuild)),
	@("CleanupMerged.ps1", @("Tests/CleanupMerged.Tests.ps1"), @($anyBuild)),
	@("CodexReview.ps1", @("Tests/CodexReview.Tests.ps1"), @($anyBuild)),
	@("PrePr.ps1", @("Tests/PrePr.Tests.ps1"), @($anyBuild)),
	@("build.ps1", @("Tests/BuildConfiguration.Tests.ps1"), @($anyBuild)),
	@("Restore.ps1", @("Tests/Restore.Tests.ps1"), @($anyBuild)),
	@("WorkerStatus.ps1", @("Tests/WorkerTools.Tests.ps1"), @($anyBuild)),
	@(".claude/skills/gui-check/gui.ps1", @("Tests/GuiCheck.Tests.ps1"), @($anyBuild)),
	@(".claude/skills/gui-check/start-gui-check.ps1", @("Tests/GuiCheck.Tests.ps1"), @($anyBuild)),
	@("Tests/guard_gui_images.py", @("Tests/guard_gui_images_test.py"), @($anyBuild)),
	@("Directory.Build.props", @($native, $editor), @())
)
foreach ($case in $cases) {
	$plan = Get-Plan @($case[0])
	$commands = @($plan.steps.Command)
	Check ($commands[0] -eq $conventions) "$($case[0]): check_conventions runs first"
	foreach ($expected in $case[1]) {
		Check (@($commands | Where-Object { $_.Contains($expected) }).Count -gt 0) "$($case[0]): runs $expected"
	}
	foreach ($unexpected in $case[2]) {
		Check (@($commands | Where-Object { $_.Contains($unexpected) }).Count -eq 0) "$($case[0]): does not run $unexpected"
	}
}

$plan = Get-Plan @("DXForgeEngine/A.cpp", "DXForgeEditor/GameProject/A.cs", "DXForgeEditor/GameProject/B.cs", "docs/A.md")
$commands = @($plan.steps.Command)
Check (@($commands | Where-Object { $_ -eq $native }).Count -eq 1 -and @($commands | Where-Object { $_ -eq $editor }).Count -eq 1) "builds are not repeated"
Check (@($commands | Where-Object { $_ -match "projects_path_test" }).Count -eq 1) "tests are not repeated"
Check ([array]::IndexOf($commands, $editor) -lt [array]::IndexOf(@($commands), (@($commands | Where-Object { $_ -match "projects_path_test" })[0]))) "builds run before tests"

$plan = Get-Plan @("DXForgeEditor/Content/A.cs")
$geometry = @($plan.steps | Where-Object { $_.Command -match "default_geometry_test" })[0]
Check (@($geometry.Requires) -contains "x64/DebugEditor/ContentTools.dll") "a test that needs ContentTools.dll is skipped without it"
$plan = Get-Plan @("Tests/guard_review_flow_test.py")
Check (@($plan.steps | Where-Object { @($_.Requires) -contains "Tests/guard_review_flow_test.py" }).Count -eq 1) "a changed test is skipped when it was deleted"
# 変更されたテストでも、DLL の条件を落とさない(PR #196 の Codex の指摘)
$plan = Get-Plan @("DXForgeEditor/Tests/default_geometry_test.ps1")
$geometry = @($plan.steps | Where-Object { $_.Command -match "default_geometry_test" })[0]
Check (@($geometry.Requires) -contains "x64/DebugEditor/ContentTools.dll" -and @($geometry.Requires) -contains "DXForgeEditor/Tests/default_geometry_test.ps1") "a changed test keeps both conditions"

# 表から選ばれたテストを同じ差分で削除しても、飛ばせる(PR #196 の Codex の指摘)。順序は問わない
foreach ($files in @(
		@("DXForgeEditor/Content/M.cs", "DXForgeEditor/Tests/material_asset_test.ps1"),
		@("DXForgeEditor/Tests/material_asset_test.ps1", "DXForgeEditor/Content/M.cs"),
		@("DXForgeEditor/GameDev/EnginePath.cs", "DXForgeEditor/Tests/engine_path_test.ps1"),
		@("DXForgeEditor/GameProject/A.cs", "DXForgeEditor/Tests/projects_path_test.ps1"),
		@("DXForgeEditor/DXForgeEditor.csproj", "DXForgeEditor/Tests/debug_define_test.ps1")
	)) {
	$plan = Get-Plan $files
	$test = $files -match '/Tests/' | Select-Object -First 1
	$matching = @($plan.steps | Where-Object { $_.Command -match [regex]::Escape($test) })
	Check ($matching.Count -eq 1 -and @($matching[0].Requires) -contains $test) "$($files -join ' + '): one step, skipped when the test was deleted"
}
$plan = Get-Plan @("DXForgeEditor/Content/M.cs", "DXForgeEditor/Tests/default_geometry_test.ps1")
$geometry = @($plan.steps | Where-Object { $_.Command -match "default_geometry_test" })
Check ($geometry.Count -eq 1 -and @($geometry[0].Requires).Count -eq 2) "the conditions are merged without repeating"

$plan = Get-Plan @("foo/bar.xyz")
Check (@($plan.steps).Count -eq 1 -and @($plan.unclassified)[0] -eq "foo/bar.xyz") "an unknown file is reported and builds nothing"

Write-Host "== -PlanOnly =="
$json = & pwsh -NoProfile -Command "& '$ScriptPath' -PlanOnly -Files 'docs/A.md','DXForgeEngine/A.cpp' -AddedText 'File.Move(a, b)'; exit `$LASTEXITCODE" | Out-String | ConvertFrom-Json
Check (@($json.steps.Command) -contains $native -and @($json.checklist).Count -eq 1) "-PlanOnly prints the plan as JSON without git"

Write-Host "== 自己点検の項目 =="
$save = "ファイルの保存や一時ファイル"
$load = "読み込み:"
$ui = "UI:"
$test = "テストの追加:"
function Get-Checklist([string[]]$Files, [string]$AddedText = "") { @((Get-Plan $Files $AddedText).checklist) -join "`n" }
Check ((Get-Checklist @("DXForgeEditor/A.cs") "+File.WriteAllText(path, x);").Contains($save)) "saving"
Check ((Get-Checklist @("DXForgeEditor/A.cs") "+var p = Path.GetTempPath();").Contains($save)) "a temporary file"
Check ((Get-Checklist @("DXForgeEditor/A.cs") "+Serializer.FromFile<A>(path);").Contains($load)) "loading"
Check ((Get-Checklist @("DXForgeEditor/A.xaml")).Contains($ui)) "a xaml file"
Check ((Get-Checklist @("DXForgeEditor/A.xaml.cs")).Contains($ui)) "a xaml code-behind"
Check ((Get-Checklist @("DXForgeEditor/Tests/a_test.ps1")).Contains($test)) "a test script"
Check ((Get-Checklist @("EngineUnitTests/Tests_A.cpp")).Contains($test)) "a C++ test"
$none = Get-Checklist @("docs/A.md") "+plain text"
Check (-not $none) "nothing for plain documentation"
Check ((Get-Checklist @("DXForgeEditor/A.cs") "+int x = 1;") -eq "") "nothing for an unrelated change"

Write-Host "== スタンプ(使い捨てのリポジトリ) =="
$env:GIT_AUTHOR_NAME = "test"; $env:GIT_AUTHOR_EMAIL = "test@example.com"
$env:GIT_COMMITTER_NAME = "test"; $env:GIT_COMMITTER_EMAIL = "test@example.com"
$env:GIT_CONFIG_COUNT = "2"
$env:GIT_CONFIG_KEY_0 = "commit.gpgsign"; $env:GIT_CONFIG_VALUE_0 = "false"
$env:GIT_CONFIG_KEY_1 = "core.autocrlf"; $env:GIT_CONFIG_VALUE_1 = "false"
function Invoke-Git {
	$output = git @args 2>&1
	if ($LASTEXITCODE -ne 0) { throw "git $args に失敗しました: $output" }
	$output
}

$repo = Join-Path ([System.IO.Path]::GetTempPath()) ("dxforge_prepr_test_" + [guid]::NewGuid().ToString("N"))
try {
	New-Item -ItemType Directory -Path (Join-Path $repo "Tests") | Out-Null
	Invoke-Git init -q -b develop $repo | Out-Null
	Copy-Item -LiteralPath $ScriptPath -Destination (Join-Path $repo "PrePr.ps1")
	$check = Join-Path $repo "Tests/check_conventions.py"
	$passing = 'print("conventions ok: this line must stay in the log")'
	# Tests/check_conventions.py を変更すると、その対応表から check_conventions_test.py も実行される
	Set-Content -LiteralPath (Join-Path $repo "Tests/check_conventions_test.py") -Value 'print("fake test")' -Encoding utf8NoBOM
	$failing = 'import sys; print("LAST-LINES-OF-THE-FAILURE"); sys.exit(1)'
	Set-Content -LiteralPath $check -Value $passing -Encoding utf8NoBOM
	Invoke-Git -C $repo add . | Out-Null
	Invoke-Git -C $repo commit -q -m first | Out-Null
	Invoke-Git -C $repo branch base | Out-Null
	Set-Content -LiteralPath (Join-Path $repo "doc.md") -Value "text" -Encoding utf8NoBOM
	# 日本語のファイル名が git に引用されると、拡張子を読めず「分類できないファイル」になる
	Set-Content -LiteralPath (Join-Path $repo "日本語.md") -Value "text" -Encoding utf8NoBOM
	Invoke-Git -C $repo add . | Out-Null
	Invoke-Git -C $repo commit -q -m doc | Out-Null

	$stampPath = Join-Path $repo (Invoke-Git -C $repo rev-parse --git-path dxforge-prepr-stamp).Trim()
	$pwshPath = (Get-Command pwsh).Source
	function Invoke-PrePr {
		Push-Location $repo
		try {
			$output = & $pwshPath -NoProfile -File (Join-Path $repo "PrePr.ps1") -Base base 2>&1 | Out-String
			[pscustomobject]@{ Exit = $LASTEXITCODE; Output = $output }
		}
		finally {
			Pop-Location
		}
	}

	$r = Invoke-PrePr
	$head = (Invoke-Git -C $repo rev-parse HEAD).Trim()
	Check ($r.Exit -eq 0) "a documentation-only diff passes (実際: $($r.Exit)、$($r.Output))"
	Check ((Test-Path -LiteralPath $stampPath) -and (Get-Content -Raw -LiteralPath $stampPath).Trim() -eq $head) "the stamp holds the sha of HEAD"
	Check ($r.Output -match "自己点検") "the output asks for the self-check section"
	Check ($r.Output -notmatch "分類できないファイル") "a Japanese file name is classified"
	Check ($r.Output -notmatch "this line must stay in the log") "the output of a passing step is not printed"

	Set-Content -LiteralPath (Join-Path $repo "doc.md") -Value "changed" -Encoding utf8NoBOM
	$r = Invoke-PrePr
	Check ($r.Exit -eq 2 -and $r.Output -match "doc\.md") "uncommitted changes are refused with exit code 2"
	Invoke-Git -C $repo checkout -q -- doc.md | Out-Null

	Set-Content -LiteralPath $check -Value $failing -Encoding utf8NoBOM
	Invoke-Git -C $repo add . | Out-Null
	Invoke-Git -C $repo commit -q -m "failing check" | Out-Null
	$r = Invoke-PrePr
	Check ($r.Exit -eq 1) "a failing step fails the run (実際: $($r.Exit))"
	Check ($r.Output -match "LAST-LINES-OF-THE-FAILURE") "the failure output is printed"
	Check (-not (Test-Path -LiteralPath $stampPath)) "a failed run leaves no stamp (and removes the earlier one)"

	# base との差分に残るよう、最初の内容とは変える
	Set-Content -LiteralPath $check -Value "$passing # changed" -Encoding utf8NoBOM
	Invoke-Git -C $repo add . | Out-Null
	Invoke-Git -C $repo commit -q -m "passing check" | Out-Null
	# コマンドを起動できないときは、$LASTEXITCODE が 0 のままなので成功と取り違えやすい(PR #196 の Codex の指摘)
	$savedPath = $env:PATH
	$env:PATH = @((Split-Path (Get-Command pwsh).Source), (Split-Path (Get-Command git).Source)) -join ";"
	try {
		if (Get-Command python -ErrorAction SilentlyContinue) {
			Check $false "python must not resolve in this case: $((Get-Command python).Source)"
		}
		$r = Invoke-PrePr
	}
	finally {
		$env:PATH = $savedPath
	}
	Check ($r.Exit -eq 1 -and -not (Test-Path -LiteralPath $stampPath)) "a command that cannot start fails the run and leaves no stamp (実際: $($r.Exit))"

	# pwsh がなければ、どのステップも実行できないので止まる(PR #196 の Codex の指摘)。
	# pwsh は起動時に自分のフォルダを PATH に足すので、Windows PowerShell から実行して再現する
	$windowsPowerShell = (Get-Command powershell).Source
	$env:PATH = Split-Path (Get-Command git).Source
	Push-Location $repo
	try {
		$output = & $windowsPowerShell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repo "PrePr.ps1") -Base base 2>&1 | Out-String
		$exit = $LASTEXITCODE
	}
	finally {
		Pop-Location
		$env:PATH = $savedPath
	}
	Check ($exit -eq 2 -and $output -match "pwsh" -and -not (Test-Path -LiteralPath $stampPath)) "no pwsh stops the run and leaves no stamp (実際: $exit、$output)"

	$r = Invoke-PrePr
	Check ($r.Exit -eq 0 -and (Get-Content -Raw -LiteralPath $stampPath).Trim() -eq (Invoke-Git -C $repo rev-parse HEAD).Trim()) "a new commit gets a new stamp"
	Check ($r.Output -match "ok   python Tests/check_conventions_test\.py") "changing the checker also runs its test"
}
finally {
	Remove-Item -Recurse -Force $repo -ErrorAction SilentlyContinue
}

if ($failures.Count -gt 0) {
	Write-Host "$($failures.Count) 件失敗" -ForegroundColor Red
	exit 1
}
Write-Host "すべて成功"
exit 0
