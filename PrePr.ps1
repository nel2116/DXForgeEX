<#
.SYNOPSIS
	PR を作る前に、変更したファイルに合うビルドとテストを実行し、自己点検の項目を出すスクリプト(#195)。

.DESCRIPTION
	-Base(既定 origin/develop)との差分のファイルから、実行するものを決める。
	- python Tests/check_conventions.py は常に実行する。
	- C++ を変更したら ./build.ps1 -Target native -Configuration Release -RunTests -WarningsAsErrors。
	  DXForgeEngineDLL と ContentTools は Debug/Release では作られないので、DebugEditor のビルドとテストも実行する。
	- C#/XAML を変更したら ./build.ps1 -Target editor -WarningsAsErrors と、対応するテスト(Get-PrePrPlan の表)。
	- 文書やスキルだけならビルドしない。
	実行は最初の失敗で止まる。成功したステップは 1 行だけ、失敗したステップは出力の末尾だけを出す(文脈を節約するため)。

	すべて通ったら、git rev-parse --git-path dxforge-prepr-stamp の場所(worktree ごと)に HEAD の sha を書く。
	Tests/guard_review_flow.py(PreToolUse フック)は、この sha が HEAD と違うときの gh pr create を止める。
	スタンプでは自己点検の項目を読んだことまでは確かめられないので、結果を PR 本文の「自己点検」の節に書くこと。

	コミットしていない変更(追跡中のファイル)があると、スタンプの sha と確かめた内容が食い違うので実行しない。

.PARAMETER Base
	比べる相手。既定は origin/develop(実行の前に fetch する)。

.PARAMETER PlanOnly
	実行せず、計画を JSON で出す。-Files と -AddedText を渡すと git を読まない。

.PARAMETER Files
	-PlanOnly で使う、変更したファイルの一覧(リポジトリからの相対パス、区切りは /)。

.PARAMETER AddedText
	-PlanOnly で使う、追加した行の本文(自己点検の項目を決める)。

.EXAMPLE
	./PrePr.ps1
#>
[CmdletBinding()]
param(
	[string]$Base = "origin/develop",
	[switch]$PlanOnly,
	[string[]]$Files,
	[string]$AddedText = ""
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$RepoRoot = $PSScriptRoot
$StampName = "dxforge-prepr-stamp"

# 変更したファイルから、実行するビルドとテストを決める。ステップは { Command, Requires }。
# Requires のファイルがないステップは、理由を出して飛ばす(FBX SDK が要る ContentTools.dll、削除したテストなど)
function Get-PrePrPlan([string[]]$ChangedFiles) {
	$nativeBuild = "./build.ps1 -Target native -Configuration Release -RunTests -WarningsAsErrors"
	$editorBuild = "./build.ps1 -Target editor -WarningsAsErrors"
	$dllBuild = "./build.ps1 -Target native -Configuration DebugEditor -Project DXForgeEngineDLL -WarningsAsErrors"
	$toolsBuild = "./build.ps1 -Target native -Configuration DebugEditor -Project ContentTools -WarningsAsErrors"
	$toolsDll = "x64/DebugEditor/ContentTools.dll"

	function Editor([string]$Name) { "pwsh -STA -NoProfile -File DXForgeEditor/Tests/$Name.ps1" }
	$layout = Editor "project_layout_view_test"
	$material = Editor "material_asset_test"
	$dialog = Editor "primitive_mesh_dialog_test"
	$geometry = Editor "default_geometry_test"
	$define = "pwsh -NoProfile -File DXForgeEditor/Tests/debug_define_test.ps1"
	$loadProject = "pwsh -STA -NoProfile -File DXForgeCli/Tests/load_project_cli_test.ps1"
	$ops = "pwsh -STA -NoProfile -File DXForgeCli/Tests/ops_cli_test.ps1"
	$importCli = "pwsh -STA -NoProfile -File DXForgeImport/Tests/import_cli_test.ps1 -ArgumentsOnly"

	# DXForgeEditor の直下のフォルダ -> テスト(docs/dev/BuildAndTest.md の一覧から)。ないフォルダは $layout
	$editorTests = @{
		"Automation"  = @($ops)
		"Components"  = @($material, $loadProject, $geometry)
		"Content"     = @($material, $geometry, $dialog)
		"DllWrappers" = @((Editor "engine_init_test"), $material, "pwsh -STA -NoProfile -File DXForgeEngineDLL/Tests/editor_shader_compilation_test.ps1")
		"Editors"     = @($layout, $dialog)
		"GameDev"     = @($layout, "pwsh -NoProfile -File DXForgeEditor/Tests/engine_path_test.ps1")
		"GameProject" = @($layout, "pwsh -NoProfile -File DXForgeEditor/Tests/projects_path_test.ps1", $loadProject)
	}
	# リポジトリ直下などのスクリプト -> そのテスト
	$scriptTests = @{
		"build.ps1"                      = "pwsh -NoProfile -File Tests/BuildConfiguration.Tests.ps1"
		"CleanupMerged.ps1"              = "pwsh -NoProfile -File Tests/CleanupMerged.Tests.ps1"
		"CodexReview.ps1"                = "pwsh -NoProfile -File Tests/CodexReview.Tests.ps1"
		"PrePr.ps1"                      = "pwsh -NoProfile -File Tests/PrePr.Tests.ps1"
		"Restore.ps1"                    = "pwsh -NoProfile -File Tests/Restore.Tests.ps1"
		"StartWorker.ps1"                = "pwsh -NoProfile -File Tests/WorkerTools.Tests.ps1"
		"WorkerStatus.ps1"               = "pwsh -NoProfile -File Tests/WorkerTools.Tests.ps1"
		"Tests/check_conventions.py"     = "python Tests/check_conventions_test.py"
		"Tests/context_size_warning.py"  = "python Tests/context_size_warning_test.py"
		"Tests/guard_merge_approval.py"  = "python Tests/guard_merge_approval_test.py"
		"Tests/guard_review_flow.py"     = "python Tests/guard_review_flow_test.py"
		"Tests/usage_report.py"          = "python Tests/usage_report_test.py"
	}

	$builds = [System.Collections.Generic.List[string]]::new()
	$tests = [System.Collections.Generic.List[object]]::new()
	$unclassified = [System.Collections.Generic.List[string]]::new()
	$addBuild = { param($command) if ($command -notin $builds) { $builds.Add($command) } }
	$addTest = {
		param($command, $requires)
		# ContentTools.dll が要るテストは、ここで決める(テストの名前が表の外から渡されても同じ)
		if (-not $requires -and ($command -match "default_geometry_test|primitive_mesh_dialog_test|ContentTools/Tests/")) { $requires = $toolsDll }
		if ($command -notin $tests.Command) { $tests.Add([pscustomobject]@{ Command = $command; Requires = $requires }) }
	}

	foreach ($file in $ChangedFiles) {
		if (-not $file) { continue }
		$file = $file.Replace("\", "/")
		$top = ($file -split "/")[0]
		$extension = [System.IO.Path]::GetExtension($file).ToLowerInvariant()
		$isCpp = $extension -in ".c", ".cpp", ".h", ".hpp", ".inl", ".hlsl", ".hlsli", ".vcxproj", ".filters", ".def", ".rc"
		$isPackages = [System.IO.Path]::GetFileName($file) -eq "packages.config"
		$isCSharp = $extension -in ".cs", ".xaml", ".csproj", ".resx"

		if ($scriptTests.ContainsKey($file)) {
			& $addTest $scriptTests[$file] $null
		}
		elseif ($file -in "DXForge.sln", "Directory.Build.props") {
			& $addBuild $nativeBuild
			& $addBuild $editorBuild
		}
		elseif ($extension -eq ".md" -or $top -in "docs", "Licenses", ".claude", ".github" -or $file -in "memo.txt", ".editorconfig", ".gitignore", ".gitattributes", "CLAUDE.md", "AGENTS.md") {
			# 文書やスキル、設定はビルドしない
		}
		elseif ($file -match '^EngineUnitTests/Tests/') {
			# build.ps1 -RunTests が実行する(基準画像も同じ)
			& $addBuild $nativeBuild
		}
		elseif ($file -match '(^|/)Tests/[^/]+(_test\.(py|ps1)|\.Tests\.ps1)$') {
			# テストを変更したら、そのテストを実行する。エディタ側のテストにはエディタのビルドが要る
			$command = if ($extension -eq ".py") { "python $file" } elseif ($top -eq "Tests") { "pwsh -NoProfile -File $file" } else { "pwsh -STA -NoProfile -File $file" }
			if ($top -in "DXForgeEditor", "DXForgeCli", "DXForgeImport") { & $addBuild $editorBuild }
			if ($top -eq "DXForgeEngineDLL") { & $addBuild $dllBuild }
			if ($top -eq "ContentTools") { & $addBuild $toolsBuild }
			& $addTest $command $file
		}
		elseif ($top -eq "DXForgeEditor" -and ($isCSharp -or $extension -in ".hlsl", ".hlsli")) {
			& $addBuild $editorBuild
			if ($extension -eq ".csproj") { & $addTest $define $null; continue }
			$folder = ($file -split "/")[1]
			foreach ($test in $(if ($editorTests.ContainsKey($folder)) { $editorTests[$folder] } else { @($layout) })) { & $addTest $test $null }
		}
		elseif ($top -eq "DXForgeCli" -and $isCSharp) {
			& $addBuild $editorBuild
			& $addTest $loadProject $null
			& $addTest $ops $null
		}
		elseif ($top -eq "DXForgeImport" -and $isCSharp) {
			& $addBuild $editorBuild
			& $addTest $importCli $null
			if ($extension -eq ".csproj") { & $addTest $define $null }
		}
		elseif ($top -eq "DXForgeEngineDLL" -and ($isCpp -or $isPackages)) {
			& $addBuild $nativeBuild
			& $addBuild $dllBuild
			& $addTest "python DXForgeEngineDLL/Tests/shader_compilation_test.py" $null
			& $addTest "pwsh -STA -NoProfile -File DXForgeEngineDLL/Tests/editor_shader_compilation_test.ps1" $null
		}
		elseif ($top -eq "ContentTools" -and ($isCpp -or $isPackages)) {
			& $addBuild $toolsBuild
			& $addTest "python ContentTools/Tests/missing_source_import_test.py" $null
			& $addTest "python ContentTools/Tests/primitive_mesh_cube_test.py" $null
		}
		elseif ($top -in "DXForgeEngine", "EngineUnitTests", "EngineTest" -and ($isCpp -or $isPackages)) {
			& $addBuild $nativeBuild
		}
		else {
			$unclassified.Add($file)
		}
	}
	[pscustomobject]@{ Builds = @($builds); Tests = @($tests); Unclassified = @($unclassified) }
}

# 実行する順: 規約の検査、ビルド、テスト
function Get-PrePrSteps($Plan) {
	$conventions = [pscustomobject]@{ Command = "python Tests/check_conventions.py"; Requires = $null }
	return @($conventions) + @($Plan.Builds | ForEach-Object { [pscustomobject]@{ Command = $_; Requires = $null } }) + @($Plan.Tests)
}

# 差分の種類に応じた自己点検の項目(#190、#191 などの Codex の指摘から)
function Get-PrePrChecklist([string[]]$ChangedFiles, [string]$Added) {
	$items = [System.Collections.Generic.List[string]]::new()
	if ($Added -match 'File\.(Write|Move|Copy|Delete|Create|Replace|Open|Append)|WriteAll|GetTemp|Set-Content|Out-File|Add-Content|Move-Item|Copy-Item|ofstream|CreateFile|fopen|tempfile|mkstemp|os\.replace|Serializer\.ToFile') {
		$items.Add("ファイルの保存や一時ファイル: 同じファイルに並行で実行したとき、権限がないときにどうなるか(File.Exists は権限がなくても例外を出さず false を返す)。一時ファイルは失敗しても残らないか。")
	}
	if ($Added -match 'File\.(Read|Open)|ReadAll|Deserialize|Serializer\.FromFile|Get-Content|ConvertFrom-Json|ifstream|Load[A-Z(]|json\.load') {
		$items.Add("読み込み: 途中で失敗したときに後始末されるか(半端な状態が残らないか)。結果が null や空のときに落ちないか。")
	}
	if (@($ChangedFiles | Where-Object { $_ -match '\.xaml(\.cs)?$' }).Count -gt 0) {
		$items.Add("UI: フォーカスの順序(Tab、閉じたあと)。既定のウィンドウの大きさではみ出さないか、ボタンが隠れないか。途中で閉じたときの動作。")
	}
	if (@($ChangedFiles | Where-Object { $_ -match '(^|/)Tests/|(^|/)Tests_[^/]+$' }).Count -gt 0) {
		$items.Add("テストの追加: 説明どおりの条件(エンジンの DLL なし、GPU なし、CI のランナーなど)で実際に動くか。修正前のコードで失敗するか。")
	}
	return @($items)
}

function Write-PrePrFailureTail([string]$Log) {
	if (Test-Path -LiteralPath $Log) { Get-Content -LiteralPath $Log -Tail 40 -Encoding utf8 | ForEach-Object { Write-Host "    $_" } }
}

if ($PlanOnly -and $PSBoundParameters.ContainsKey("Files")) {
	$changed = @($Files)
	$added = $AddedText
}
else {
	Push-Location $RepoRoot
	try {
		if (-not $PlanOnly) {
			$dirty = @(git status --porcelain --untracked-files=no)
			if ($LASTEXITCODE -ne 0) { Write-Host "git status に失敗しました。"; exit 2 }
			if ($dirty.Count -gt 0) {
				Write-Host "コミットしていない変更があります。コミットしてから実行してください(スタンプは HEAD に付くため)。"
				$dirty | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" }
				exit 2
			}
		}
		if ($Base -eq "origin/develop") {
			# FETCH_HEAD は worktree で共有されるので、並行して実行しても競合しないよう書かない
			git fetch --quiet --no-write-fetch-head origin develop 2>$null
			if ($LASTEXITCODE -ne 0) { Write-Warning "git fetch に失敗しました。手元の $Base と比べます。" }
		}
		$changed = @(git diff --name-only "$Base...HEAD")
		if ($LASTEXITCODE -ne 0) { Write-Host "$Base との差分を読めませんでした。"; exit 2 }
		$added = (@(git diff -U0 "$Base...HEAD") | Where-Object { $_ -match '^\+' -and $_ -notmatch '^\+\+\+' }) -join "`n"
		if ($LASTEXITCODE -ne 0) { Write-Host "$Base との差分を読めませんでした。"; exit 2 }
	}
	finally {
		Pop-Location
	}
}

$plan = Get-PrePrPlan $changed
$steps = Get-PrePrSteps $plan
$checklist = Get-PrePrChecklist $changed $added

if ($PlanOnly) {
	ConvertTo-Json -Depth 4 -InputObject ([pscustomobject]@{ steps = @($steps); unclassified = @($plan.Unclassified); checklist = @($checklist) })
	exit 0
}

Push-Location $RepoRoot
$log = [System.IO.Path]::GetTempFileName()
try {
	$head = (git rev-parse HEAD).Trim()
	$stamp = (git rev-parse --git-path $StampName).Trim()
	# 前回のスタンプは、この実行が通るまで無効にする
	Remove-Item -LiteralPath $stamp -ErrorAction SilentlyContinue

	Write-Host "$Base との差分: $($changed.Count) ファイル、ステップ $($steps.Count) 個"
	foreach ($step in $steps) {
		if ($step.Requires -and -not (Test-Path -LiteralPath $step.Requires)) {
			Write-Host "SKIP $($step.Command)($($step.Requires) がありません)"
			continue
		}
		$watch = [System.Diagnostics.Stopwatch]::StartNew()
		$wrapped = "`$global:LASTEXITCODE = 0; $($step.Command); exit `$LASTEXITCODE"
		# 出力は文脈を消費するので、ログに書いて、失敗したときだけ末尾を出す
		$ErrorActionPreference = "Continue"
		& pwsh -NoProfile -Command $wrapped 2>&1 | Out-File -LiteralPath $log -Encoding utf8
		$exit = $LASTEXITCODE
		$ErrorActionPreference = "Stop"
		if ($exit -ne 0) {
			Write-Host "FAIL $($step.Command)(終了コード $exit)"
			Write-PrePrFailureTail $log
			Write-Host "PrePr: 失敗しました。スタンプは残していません。"
			exit 1
		}
		Write-Host "ok   $($step.Command)($([int]$watch.Elapsed.TotalSeconds) 秒)"
	}

	if ((git rev-parse HEAD).Trim() -ne $head) {
		Write-Host "PrePr: 実行中に HEAD が変わったので、スタンプは残しません。もう一度実行してください。"
		exit 1
	}
	# --git-path は worktree では絶対パスを返す。親フォルダはこの worktree の git のフォルダなので、すでにある
	[System.IO.File]::WriteAllText([System.IO.Path]::Combine((Get-Location).Path, $stamp), "$head`n", [System.Text.UTF8Encoding]::new($false))
}
finally {
	Remove-Item -LiteralPath $log -ErrorAction SilentlyContinue
	Pop-Location
}

foreach ($file in $plan.Unclassified) { Write-Host "分類できないファイル(自分で確かめる): $file" }
Write-Host ""
Write-Host "PrePr: すべて通りました。スタンプ: $($head.Substring(0, 7))"
Write-Host ""
Write-Host "自己点検(結果を PR 本文の「自己点検」の節に書く。スタンプでは読んだことまで確かめられない):"
if ($checklist.Count -eq 0) { Write-Host "- この差分に特別な項目はありません(「自己点検: 該当なし」と書く)。" }
foreach ($item in $checklist) { Write-Host "- $item" }
exit 0
