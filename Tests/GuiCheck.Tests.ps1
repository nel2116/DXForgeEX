<#
.SYNOPSIS
	gui-check スキルのスクリプト(gui.ps1、start-gui-check.ps1、pixdiff.py)のうち、GUI なしで動く部分の回帰テスト(#195)。

.DESCRIPTION
	PATH の先頭に偽の gh と orca を置くので、本物の GitHub と Orca は呼ばない。エディタは起動しない。
	x64\DebugEditor\dxforge.exe があれば、gui.ps1 -Action prepare を本物で確かめる(なければ SKIP。CI は -RequireCli で失敗にする)。

.EXAMPLE
	pwsh -NoProfile -File Tests/GuiCheck.Tests.ps1
#>
[CmdletBinding()]
param([switch]$RequireCli)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$SkillDir = Join-Path $RepoRoot ".claude\skills\gui-check"
$Gui = Join-Path $SkillDir "gui.ps1"
$Start = Join-Path $SkillDir "start-gui-check.ps1"

$failures = [System.Collections.Generic.List[string]]::new()
function Check([bool]$condition, [string]$message) {
	if ($condition) { Write-Host "  ok: $message" }
	else {
		Write-Host "  NG: $message" -ForegroundColor Red
		$failures.Add($message)
	}
}

$fakeGh = @'
Add-Content -LiteralPath (Join-Path $env:DXFORGE_TEST_STATE "gh.log") -Value "$args" -Encoding utf8
if ($env:DXFORGE_TEST_GH_FAIL) { exit 1 }
Get-Content -Raw -Encoding utf8 (Join-Path $env:DXFORGE_TEST_STATE "pr.json")
exit 0
'@
$fakeOrca = @'
Add-Content -LiteralPath (Join-Path $env:DXFORGE_TEST_STATE "orca.log") -Value "$args" -Encoding utf8
if ($args[1] -eq "worker-start") {
	$spec = $args[[array]::IndexOf($args, "--spec-file") + 1]
	Copy-Item -LiteralPath $spec -Destination (Join-Path $env:DXFORGE_TEST_STATE "spec.md")
}
if ($env:DXFORGE_TEST_ORCA_FAIL) { exit 1 }
if ($args[0] -eq "worktree" -and $args[1] -eq "rm" -and -not $env:DXFORGE_TEST_ORCA_SLOW) {
	Set-Content -LiteralPath (Join-Path $env:DXFORGE_TEST_STATE "worktrees.txt") -Value "worktree C:/repo`nHEAD 0" -Encoding utf8
}
exit 0
'@

# start-gui-check.ps1 が git で読むのは worktree の一覧だけ。本物の一覧(並行して動く GUI 確認)の影響を受けないようにする
$fakeGit = @'
$line = "$args"
Add-Content -LiteralPath (Join-Path $env:DXFORGE_TEST_STATE "git.log") -Value $line -Encoding utf8
if ($line -match "for-each-ref") { Get-Content -Encoding utf8 (Join-Path $env:DXFORGE_TEST_STATE "branches.txt"); exit 0 }
if ($line -match "rev-parse") { if ($env:DXFORGE_TEST_BRANCH_GONE) { exit 1 }; exit 0 }
if ($line -match " branch -d (.+)$") { if ($env:DXFORGE_TEST_BRANCH_FAIL) { exit 1 }; exit 0 }
if ($line -notmatch "worktree list --porcelain") { exit 1 }
Get-Content -Encoding utf8 (Join-Path $env:DXFORGE_TEST_STATE "worktrees.txt")
exit 0
'@

$root = Join-Path ([System.IO.Path]::GetTempPath()) ("dxforge_guicheck_test_" + [guid]::NewGuid().ToString("N"))
$bin = Join-Path $root "bin"
$state = Join-Path $root "state"
$savedPath = $env:PATH
try {
	New-Item -ItemType Directory -Path $bin, $state | Out-Null
	Set-Content -LiteralPath (Join-Path $bin "gh.ps1") -Value $fakeGh -Encoding utf8
	Set-Content -LiteralPath (Join-Path $bin "orca.ps1") -Value $fakeOrca -Encoding utf8
	Set-Content -LiteralPath (Join-Path $bin "git.ps1") -Value $fakeGit -Encoding utf8
	Set-Content -LiteralPath (Join-Path $state "worktrees.txt") -Value "worktree C:/repo`nHEAD 0" -Encoding utf8
	Set-Content -LiteralPath (Join-Path $state "branches.txt") -Value "develop" -Encoding utf8
	$env:PATH = "$bin;$savedPath"
	$env:DXFORGE_TEST_STATE = $state
	# 本物の start-gui-check.ps1 と Mutex を共有しない
	$env:DXFORGE_GUI_CHECK_MUTEX = "Local\DXForgeEX_GuiCheckTest_" + [guid]::NewGuid().ToString("N")
	foreach ($tool in "gh", "orca", "git") {
		if ((Get-Command $tool).Source -notlike "$bin*") { throw "偽の $tool が解決されません" }
	}
	$orcaLog = Join-Path $state "orca.log"

	function Invoke-Script([string]$Script, [string[]]$Arguments) {
		Remove-Item -LiteralPath $orcaLog -ErrorAction SilentlyContinue
		$output = & pwsh -NoProfile -File $Script @Arguments 2>&1 | Out-String
		[pscustomobject]@{ Exit = $LASTEXITCODE; Output = $output; Log = $(if (Test-Path $orcaLog) { Get-Content -Raw -Encoding utf8 $orcaLog } else { "" }) }
	}
	function Set-Pr([string]$Body, [string]$PrState = "OPEN") {
		$pr = [pscustomobject]@{ url = "https://example.test/pull/7"; title = "Title {{HEAD}}"; state = $PrState; body = $Body; headRefName = "fix/issue-7-x"; headRefOid = "a" * 40; baseRefName = "develop" }
		[System.IO.File]::WriteAllText((Join-Path $state "pr.json"), (ConvertTo-Json -InputObject $pr), [System.Text.UTF8Encoding]::new($false))
	}

	Write-Host "== 構文 =="
	foreach ($file in "gui.ps1", "start-gui-check.ps1", "window.ps1", "push-pr-assets.ps1", "capture-debug-output.ps1") {
		$errors = $null
		[void][System.Management.Automation.Language.Parser]::ParseFile((Join-Path $SkillDir $file), [ref]$null, [ref]$errors)
		Check ($errors.Count -eq 0) "$file parses"
	}

	Write-Host "== gui.ps1 の引数(ウィンドウに触らない)=="
	$r = Invoke-Script $Gui @("-Action", "names")
	Check ($r.Exit -eq 0 -and $r.Output -match "File \[DXForge Editor\] 21,40" -and $r.Output -match "CreateTab \[Project Browser\] el:Button" -and $r.Output -match "region Hierarchy") "names lists the named targets and regions"
	foreach ($bad in @(
			@("-Action", "open"),
			@("-Action", "create"),
			@("-Action", "create", "-Project", "MyGame"),
			@("-Action", "create", "-Project", "GuiCheck 1"),
			@("-Action", "create", "-Project", "GuiCheck200-test"),
			@("-Action", "switch", "-Project", "GuiCheck1"),
			@("-Action", "close"),
			@("-Action", "click", "-ProcId", "1"),
			@("-Action", "drag", "-ProcId", "1", "-Target", "File"),
			@("-Action", "keys"),
			@("-Action", "shot", "-ProcId", "1"),
			@("-Action", "wait", "-ProcId", "1"),
			@("-Action", "move", "-ProcId", "1"),
			@("-Action", "prepare"),
			@("-Action", "prepare", "-Project", "GuiCheck1"),
			@("-Action", "remove-project"),
			@("-Action", "remove-project", "-Project", "MyGame"),
			@("-Action", "remove-project", "-Project", "..\GuiCheck1"),
			@("-Action", "diff")
		)) {
		$r = Invoke-Script $Gui $bad
		Check ($r.Exit -eq 2) "refused: $($bad -join ' ') (実際: $($r.Exit))"
	}
	$r = Invoke-Script $Gui @("-Action", "bogus")
	Check ($r.Exit -ne 0) "an unknown action is refused"

	Write-Host "== gui.ps1 -Action remove-project =="
	$projects = Join-Path $root "projects"
	New-Item -ItemType Directory -Path (Join-Path $projects "GuiCheck1\.DXForge"), (Join-Path $projects "MyGame") | Out-Null
	$rootArgs = @("-ProjectsRoot", $projects)
	$lock = Join-Path $projects "GuiCheck1\.DXForge\editor.lock"
	Set-Content -LiteralPath $lock -Value "" -Encoding utf8
	$held = [System.IO.File]::Open($lock, "Open", "ReadWrite", "ReadWrite")
	$r = Invoke-Script $Gui (@("-Action", "remove-project", "-Project", "GuiCheck1") + $rootArgs)
	$held.Dispose()
	Check ($r.Exit -eq 1 -and (Test-Path (Join-Path $projects "GuiCheck1"))) "a project with a held lock stays"
	$r = Invoke-Script $Gui (@("-Action", "remove-project", "-Project", "GuiCheck1") + $rootArgs)
	Check ($r.Exit -eq 0 -and -not (Test-Path (Join-Path $projects "GuiCheck1")) -and (Test-Path (Join-Path $projects "MyGame"))) "removes only the named project"
	$r = Invoke-Script $Gui (@("-Action", "remove-project", "-Project", "GuiCheck1") + $rootArgs)
	Check ($r.Exit -eq 0) "a missing project is not an error"

	Write-Host "== gui.ps1 -Action prepare =="
	$cli = Join-Path $RepoRoot "x64\DebugEditor\dxforge.exe"
	$r = Invoke-Script $Gui (@("-Action", "prepare", "-Project", "GuiCheckNone", "-Op", '{"op":"list-entities"}') + $rootArgs)
	Check ($r.Exit -eq 2 -and $r.Output -match "プロジェクトがありません") "a missing project stops before the tools"
	if (Test-Path -LiteralPath $cli) {
		$copy = Join-Path $projects "GuiCheckOps"
		New-Item -ItemType Directory -Path $copy | Out-Null
		$dxproj = Join-Path $copy "GuiCheckOps.dxproj"
		Copy-Item -LiteralPath (Join-Path $RepoRoot "DXForgeCli\Tests\Projects\TransformOnly\TransformOnly.dxproj") -Destination $dxproj
		$r = Invoke-Script $Gui (@("-Action", "prepare", "-Project", "GuiCheckOps", "-Op", '{"op":"create-entity","name":"Box"}') + $rootArgs)
		Check ($r.Exit -eq 0 -and $r.Output -match '"changed":true,"saved":true' -and (Get-Content -Raw -LiteralPath $dxproj) -match "Box") "prepare runs dxforge --op and saves"
		$r = Invoke-Script $Gui (@("-Action", "prepare", "-Project", "GuiCheckOps", "-Op", '{"op":"rename","target":"name:NoSuch","name":"X"}') + $rootArgs)
		Check ($r.Exit -eq 41) "prepare returns the exit code of dxforge (実際: $($r.Exit))"
	}
	elseif ($RequireCli) { Check $false "dxforge.exe is required: $cli" }
	else { Write-Host "  SKIP: $cli がありません" }

	Write-Host "== gui.ps1 -Action close(エディタでないウィンドウ)=="
	$form = Start-Process pwsh -PassThru -ArgumentList @("-NoProfile", "-Command", "Add-Type -AssemblyName System.Windows.Forms; `$f = New-Object System.Windows.Forms.Form; `$f.Text = 'DXForge Game'; [System.Windows.Forms.Application]::Run(`$f)")
	try {
		$deadline = (Get-Date).AddSeconds(30)
		while ((Get-Process -Id $form.Id).MainWindowHandle -eq [IntPtr]::Zero -and (Get-Date) -lt $deadline) { Start-Sleep -Milliseconds 200 }
		$r = Invoke-Script $Gui @("-Action", "close", "-ProcId", "$($form.Id)")
		Check ($r.Exit -eq 0 -and $r.Output -match "終了しました" -and $form.HasExited) "closes a game window that is not titled like the editor (実際: $($r.Exit) $($r.Output))"
	}
	finally {
		if (-not $form.HasExited) { Stop-Process -Id $form.Id -Force }
	}

	Write-Host "== pixdiff.py =="
	$images = Join-Path $root "images"
	New-Item -ItemType Directory -Path $images | Out-Null
	$makePng = Join-Path $root "make_png.py"
	Set-Content -LiteralPath $makePng -Encoding utf8 -Value @'
import struct, sys, zlib
def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
path, value, size = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
rows = b"".join(b"\x00" + bytes([value, 0, 0]) * size for _ in range(size))
data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", size, size, 8, 2, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(rows)) + chunk(b"IEND", b"")
open(path, "wb").write(data)
'@
	foreach ($spec in @(@("base-same", 10, 3), @("pr-same", 10, 3), @("base-other", 10, 3), @("pr-other", 40, 3))) {
		& python $makePng (Join-Path $images "$($spec[0]).png") $spec[1] $spec[2]
	}
	$r = Invoke-Script $Gui @("-Action", "diff", "-Dir", $images)
	Check ($r.Exit -eq 0 -and $r.Output -match "same\.png diff_pixels=0 identical" -and $r.Output -match "other\.png diff_pixels=9 x 0-2 y 0-2 max_channel_delta 30") "reports identical images and the size of a difference"
	& python $makePng (Join-Path $images "base-size.png") 10 3
	& python $makePng (Join-Path $images "pr-size.png") 10 4
	$r = Invoke-Script $Gui @("-Action", "diff", "-Dir", $images)
	Check ($r.Exit -eq 1 -and $r.Output -match "size\.png size differs 3x3 vs 4x4") "a size difference fails"

	Write-Host "== start-gui-check.ps1 =="
	$body = @'
## 概要

1. これは手順ではない

## GUI の確認手順

準備の説明。

1. エディタを開く。
2. {{HEAD}} はそのまま残す。

```text
# コードの中の見出しは節を閉じない
```

### 細かい注意

子の見出しは節に含む。

## 動作確認

3. これも手順ではない

<!-- gui-check:start -->
結果
<!-- gui-check:end -->

Closes #7

🤖 Generated with [Claude Code](https://claude.com/claude-code)
'@
	Set-Pr $body
	$r = Invoke-Script $Start @("-Pr", "7", "-DryRun")
	$steps = $r.Output.Substring($r.Output.IndexOf("### 手順"))
	Check ($r.Exit -eq 0 -and -not $r.Log) "-DryRun does not call orca"
	Check ($r.Output -match "--worktree new-top-level --name gui-check-pr-7 --base-branch origin/develop --setup run --model claude-sonnet-5-5") "a new worktree from origin/develop, on the small model"
	Check ($r.Output -match "git fetch origin refs/pull/7/head" -and $r.Output -match ("git checkout --detach " + ("a" * 40))) "the worker fetches the PR ref and checks out the head"
	Check ($steps -match "1\. エディタを開く" -and $steps -match "コードの中の見出しは節を閉じない" -and $steps -match "子の見出しは節に含む") "takes the section with code and sub-headings"
	Check ($steps -notmatch "これは手順ではない|これも手順ではない|Closes|Generated") "does not take other sections"
	Check ($steps -match "2\. \{\{HEAD\}\} はそのまま残す") "placeholders in the steps stay as written"
	Check ($r.Output -match ("確かめる head: ``" + ("a" * 40)) -and $r.Output -match "ブランチ ``fix/issue-7-x``" -and $r.Output -match "Title \{\{HEAD\}\}") "the spec carries the head and keeps the title as written"
	Check ($r.Output -match "worker 共通のルール" -and $r.Output -match "共通のルールとの違い") "the spec is the common rules, then the fixed rules"
	Check ($r.Output -notmatch "\{\{(PR|URL|TITLE|HEAD_REF|BASE_REF|STEPS)\}\}") "no placeholder is left"

	Set-Pr "## GUI の確認手順`n`n1. 手順`n`n<!-- gui-check:start -->`n2. 結果の節の中`n<!-- gui-check:end -->"
	$r = Invoke-Script $Start @("-Pr", "7", "-DryRun")
	Check ($r.Exit -eq 0 -and $r.Output -match "1\. 手順" -and $r.Output -notmatch "結果の節の中") "stops at the result section"

	Set-Pr "## GUI の確認手順`n`n番号のない説明だけ。`n`nCloses #7"
	$r = Invoke-Script $Start @("-Pr", "7")
	Check ($r.Exit -eq 3 -and -not $r.Log) "a section without numbered steps is refused"

	Set-Pr "## 動作確認`n`n1. 手順の節がない"
	$r = Invoke-Script $Start @("-Pr", "7")
	Check ($r.Exit -eq 3 -and -not $r.Log) "a body without the section is refused"

	Set-Pr "## GUI の確認手順`n`n1. x" "MERGED"
	$r = Invoke-Script $Start @("-Pr", "7")
	Check ($r.Exit -eq 2 -and -not $r.Log) "a closed PR is refused"

	$env:DXFORGE_TEST_GH_FAIL = "1"
	$r = Invoke-Script $Start @("-Pr", "7")
	$env:DXFORGE_TEST_GH_FAIL = ""
	Check ($r.Exit -eq 1 -and -not $r.Log) "a failed gh is an error"

	Set-Pr "## GUI の確認手順`n`n1. 起動の確認。`n`nCloses #7"
	# 並行して動く本物の起動の一時ファイルを数えないよう、この実行の一時フォルダを分ける
	$specTemp = Join-Path $root "temp"
	New-Item -ItemType Directory -Path $specTemp | Out-Null
	$savedTmp = $env:TMP
	$env:TMP = $specTemp
	$r = Invoke-Script $Start @("-Pr", "7", "-Size", "normal", "-Run", "run_x")
	$env:TMP = $savedTmp
	$sent = Get-Content -Raw -Encoding utf8 (Join-Path $state "spec.md")
	Check ($r.Exit -eq 0 -and $r.Log -match "--run run_x" -and $r.Log -notmatch "--model") "normal size and the Run are passed"
	Check ($sent -match "1\. 起動の確認" -and $sent -match "worker 共通のルール") "the spec reaches the worker"
	Check ($r.Output -match "-Pr 7 -Cleanup") "tells how to clean up"
	$leftovers = @(Get-ChildItem $specTemp -Filter "dxforge_gui_check_spec_*")
	Check ($leftovers.Count -eq 0) "no temporary spec is left"

	Write-Host "== start-gui-check.ps1 の排他 =="
	$lock = [System.Threading.Mutex]::new($false, $env:DXFORGE_GUI_CHECK_MUTEX)
	try {
		[void]$lock.WaitOne(0)
		$r = Invoke-Script $Start @("-Pr", "7")
		Check ($r.Exit -eq 4 -and -not $r.Log -and $r.Output -match "起動の途中") "a second start does not wait or start while the lock is held (実際: $($r.Exit))"
		$r = Invoke-Script $Start @("-Pr", "7", "-DryRun")
		Check ($r.Exit -eq 0) "-DryRun does not need the lock (実際: $($r.Exit))"
		$lock.ReleaseMutex()

		# このプロセスがハンドルを持ち続けるので、Mutex は消えずに放棄された状態になる
		& pwsh -NoProfile -Command '$m = [System.Threading.Mutex]::new($false, $env:DXFORGE_GUI_CHECK_MUTEX); if (-not $m.WaitOne(0)) { exit 9 }; exit 0'
		Check ($LASTEXITCODE -eq 0) "a process took the lock and ended without releasing it"
		$r = Invoke-Script $Start @("-Pr", "7")
		Check ($r.Exit -eq 0 -and $r.Log -match "worker-start") "an abandoned lock counts as taken (実際: $($r.Exit) $($r.Output))"

		Check $lock.WaitOne(0) "the lock is released after a start"
		$lock.ReleaseMutex()
	}
	finally {
		$lock.Dispose()
	}

	$r = Invoke-Script $Start @("-Pr", "999999", "-Cleanup")
	Check ($r.Exit -eq 0 -and $r.Output -match "ありません: gui-check-pr-999999" -and -not $r.Log) "-Cleanup with no worktree does nothing"

	Set-Content -LiteralPath (Join-Path $state "worktrees.txt") -Value "worktree C:/repo`nHEAD 0`n`nworktree C:/w/gui-check-pr-999998`nHEAD 1" -Encoding utf8
	$r = Invoke-Script $Start @("-Pr", "7")
	Check ($r.Exit -eq 4 -and -not $r.Log -and $r.Output -match "gui-check-pr-999998") "a second GUI check does not start while another worktree remains (実際: $($r.Exit))"
	$r = Invoke-Script $Start @("-Pr", "999998", "-Cleanup")
	Check ($r.Exit -eq 0 -and $r.Log -match "worktree rm --worktree path:C:/w/gui-check-pr-999998 --delete-branch" -and $r.Log -notmatch "--force") "-Cleanup removes the worktree through Orca, without --force"

	# 残ったブランチの削除(#199)。-d で、接頭辞は固定にしない。別の PR の番号のブランチには触らない
	$gitLog = Join-Path $state "git.log"
	Set-Content -LiteralPath (Join-Path $state "branches.txt") -Value "develop`nnel2116/gui-check-pr-999998`ngui-check-pr-999998`nnel2116/gui-check-pr-9999981`ngui-check-pr-99999" -Encoding utf8
	Remove-Item -LiteralPath $gitLog -ErrorAction SilentlyContinue
	Set-Content -LiteralPath (Join-Path $state "worktrees.txt") -Value "worktree C:/repo`nHEAD 0`n`nworktree C:/w/gui-check-pr-999998`nHEAD 1" -Encoding utf8
	$r = Invoke-Script $Start @("-Pr", "999998", "-Cleanup")
	$g = Get-Content -Raw -Encoding utf8 $gitLog
	Check ($r.Exit -eq 0 -and $g -match "branch -d nel2116/gui-check-pr-999998" -and $g -match "branch -d gui-check-pr-999998" -and $g -notmatch "9999981" -and $g -notmatch "(?m)gui-check-pr-99999\s*$" -and $g -cnotmatch "branch -D") "-Cleanup deletes the leftover branches with -d only (実際: $($r.Exit))"
	$env:DXFORGE_TEST_BRANCH_FAIL = "1"
	Set-Content -LiteralPath (Join-Path $state "worktrees.txt") -Value "worktree C:/repo`nHEAD 0`n`nworktree C:/w/gui-check-pr-999998`nHEAD 1" -Encoding utf8
	$r = Invoke-Script $Start @("-Pr", "999998", "-Cleanup")
	Check ($r.Exit -ne 0 -and $r.Output -match "消せませんでした") "-Cleanup exits non-zero when a branch cannot be deleted"
	# 列挙のあとに Orca が先に消したブランチは成功とみなす
	$env:DXFORGE_TEST_BRANCH_GONE = "1"
	Set-Content -LiteralPath (Join-Path $state "worktrees.txt") -Value "worktree C:/repo`nHEAD 0`n`nworktree C:/w/gui-check-pr-999998`nHEAD 1" -Encoding utf8
	$r = Invoke-Script $Start @("-Pr", "999998", "-Cleanup")
	Check ($r.Exit -eq 0) "-Cleanup treats a branch that vanished meanwhile as deleted (実際: $($r.Exit))"
	$env:DXFORGE_TEST_BRANCH_GONE = $null
	$env:DXFORGE_TEST_BRANCH_FAIL = $null
	# 登録が消えないときはブランチに触らない
	$env:DXFORGE_TEST_ORCA_SLOW = "1"
	Set-Content -LiteralPath (Join-Path $state "worktrees.txt") -Value "worktree C:/repo`nHEAD 0`n`nworktree C:/w/gui-check-pr-999998`nHEAD 1" -Encoding utf8
	Remove-Item -LiteralPath $gitLog -ErrorAction SilentlyContinue
	$r = Invoke-Script $Start @("-Pr", "999998", "-Cleanup")
	Check ($r.Exit -ne 0 -and $r.Output -match "登録が残っています" -and ((Get-Content -Raw $gitLog) -notmatch "for-each-ref|branch -d")) "-Cleanup leaves branches alone while the worktree is still registered (実際: $($r.Exit))"
	$env:DXFORGE_TEST_ORCA_SLOW = $null
	# orca が失敗したときはブランチに触らない
	$env:DXFORGE_TEST_ORCA_FAIL = "1"
	Set-Content -LiteralPath (Join-Path $state "worktrees.txt") -Value "worktree C:/repo`nHEAD 0`n`nworktree C:/w/gui-check-pr-999998`nHEAD 1" -Encoding utf8
	Remove-Item -LiteralPath $gitLog -ErrorAction SilentlyContinue
	$r = Invoke-Script $Start @("-Pr", "999998", "-Cleanup")
	Check ($r.Exit -ne 0 -and -not (Test-Path $gitLog -PathType Leaf -ErrorAction SilentlyContinue | Where-Object { $_ } | ForEach-Object { (Get-Content -Raw $gitLog) -match "for-each-ref|branch -d" })) "-Cleanup does not touch branches when orca worktree rm fails (実際: $($r.Exit))"
	$env:DXFORGE_TEST_ORCA_FAIL = $null
}
finally {
	$env:PATH = $savedPath
	$env:DXFORGE_GUI_CHECK_MUTEX = $null
	Remove-Item -Recurse -Force $root -ErrorAction SilentlyContinue
}

if ($failures.Count -gt 0) {
	Write-Host "$($failures.Count) 件失敗" -ForegroundColor Red
	exit 1
}
Write-Host "すべて成功"
exit 0
