<#
.SYNOPSIS
	CleanupMerged.ps1 の回帰テスト(Issue #104)。本物のworktreeとブランチ、Orca、GitHubには触らない。

.DESCRIPTION
	ケースごとに %TEMP% の下へ使い捨てのリポジトリ(bareのorigin と、そのクローンのルート)を作り、
	テスト対象のスクリプトをルートにコミットして、ダミーのworktreeとブランチに対して実行する。
	PATH の先頭に偽の gh.ps1 と orca.ps1 を置き、本物の gh と orca が呼ばれないようにする
	(実行の前に Get-Command で偽物が解決されることを確かめ、そうでなければ失敗する)。
	- 偽の gh: 環境変数 DXFORGE_TEST_MERGED_PRS(「ブランチ=PR番号;...」)からマージ済みのPRを返す。
	- 偽の orca: worktree rm で {"ok": true} をすぐに返し、別のプロセスが2秒後にフォルダ(ロックされた
	  ファイルは残る)とworktreeの登録を消し、さらに2秒後にブランチを消す(本物のOrcaの非同期の削除の再現)。
	  DXFORGE_TEST_ORCA_MODE=keep なら何も消さない(上限まで待っても登録が残る場合の再現)。

	ケース:
	  orca-async    Orcaの削除が遅れても、警告なしにworktree、ブランチ、フォルダが消える
	  locked        フォルダの中のDLLを子プロセスが読み込んでいると、フォルダが残り、警告に子のPIDが出る
	  orca-keep     上限まで待っても登録が残るとき、警告を出し、ブランチには触らない
	  git-only      orcaがないとき、git worktree remove と git branch -D で消える
	  keep-unsafe   未マージ、マージ後のコミットあり、未コミットの変更あり、未追跡ファイルありは残る
	  dry-run       -DryRun では何も消えず、orcaも呼ばれない

.PARAMETER ScriptPath
	テストするスクリプト。既定はリポジトリの CleanupMerged.ps1。修正前のスクリプトで再現を確かめるときは、
	git show origin/develop:CleanupMerged.ps1 を保存したファイルを渡す。

.PARAMETER BaselineScriptPath
	指定すると、dry-run のケースで、このスクリプトの -DryRun の出力がテスト対象と同じことも確かめる。

.PARAMETER Case
	実行するケースの名前(省略するとすべて)。

.EXAMPLE
	pwsh -NoProfile -File Tests/CleanupMerged.Tests.ps1
#>
[CmdletBinding()]
param(
	[string]$ScriptPath = (Join-Path $PSScriptRoot "..\CleanupMerged.ps1"),
	[string]$BaselineScriptPath,
	[string[]]$Case
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$ScriptPath = (Resolve-Path $ScriptPath).Path
if ($BaselineScriptPath) { $BaselineScriptPath = (Resolve-Path $BaselineScriptPath).Path }

$failures = [System.Collections.Generic.List[string]]::new()
$currentCase = ""
function Check([bool]$condition, [string]$message) {
	if ($condition) { Write-Host "  ok: $message" }
	else {
		Write-Host "  NG: $message" -ForegroundColor Red
		$failures.Add("[$currentCase] $message")
	}
}

# 利用者のgitの設定(署名など)に左右されないようにする
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

$fakeGh = @'
# 偽の gh(テスト用)。gh pr list --head <ブランチ> にだけ答える
$head = $null
for ($i = 0; $i -lt $args.Count; $i++) { if ($args[$i] -eq "--head") { $head = $args[$i + 1] } }
foreach ($pair in "$env:DXFORGE_TEST_MERGED_PRS" -split ';') {
	$name, $number = $pair -split '=', 2
	if ($name -and $name -eq $head) { Write-Output $number }
}
exit 0
'@

$fakeOrca = @'
# 偽の orca(テスト用)。worktree rm --worktree path:<パス> にだけ答える
Add-Content -LiteralPath $env:DXFORGE_TEST_ORCA_LOG -Value "$args"
$selector = $args[[array]::IndexOf($args, "--worktree") + 1]
$path = $selector -replace '^path:', ''
if ($env:DXFORGE_TEST_ORCA_MODE -ne "keep") {
	$gitDir = (git -C $path rev-parse --absolute-git-dir)
	$commonDir = (git -C $path rev-parse --path-format=absolute --git-common-dir)
	$branch = (git -C $path rev-parse --abbrev-ref HEAD)
	$bg = Start-Process pwsh -PassThru -WindowStyle Hidden -ArgumentList @(
		"-NoProfile", "-File", (Join-Path $PSScriptRoot "orca-bg.ps1"), $path, $gitDir, $commonDir, $branch)
	Add-Content -LiteralPath $env:DXFORGE_TEST_PIDS -Value $bg.Id
}
Write-Output '{"ok": true}'
exit 0
'@

$fakeOrcaBackground = @'
# 偽の orca の非同期の削除。フォルダ(ロックされたファイルは残る)と登録を消し、遅れてブランチを消す
param([string]$Path, [string]$GitDir, [string]$CommonDir, [string]$Branch)
Start-Sleep -Seconds 2
Remove-Item -LiteralPath $Path -Recurse -Force -ErrorAction SilentlyContinue
Remove-Item -LiteralPath $GitDir -Recurse -Force -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
git --git-dir $CommonDir branch -D $Branch | Out-Null
'@

$locker = @'
# フォルダの中のDLLを読み込んだまま待つ(ロックの再現)
param([string]$Dll, [string]$Ready)
Add-Type -Namespace CleanupMergedTest -Name Kernel32 -MemberDefinition '[DllImport("kernel32", CharSet = CharSet.Unicode)] public static extern System.IntPtr LoadLibraryW(string path);'
if ([CleanupMergedTest.Kernel32]::LoadLibraryW($Dll) -eq [IntPtr]::Zero) { exit 1 }
Set-Content -LiteralPath $Ready -Value "ready"
Start-Sleep -Seconds 600
'@

# 使い捨てのリポジトリを作る。ルートは develop、origin は bare
function New-Sandbox {
	$dir = Join-Path ([IO.Path]::GetTempPath()) "cleanup-merged-test-$([guid]::NewGuid().ToString('N').Substring(0, 8))"
	$bin = Join-Path $dir "bin"
	New-Item -ItemType Directory -Force $bin | Out-Null
	Set-Content -LiteralPath (Join-Path $bin "gh.ps1") -Value $fakeGh -Encoding utf8BOM
	Set-Content -LiteralPath (Join-Path $bin "orca.ps1") -Value $fakeOrca -Encoding utf8BOM
	Set-Content -LiteralPath (Join-Path $bin "orca-bg.ps1") -Value $fakeOrcaBackground -Encoding utf8BOM
	Set-Content -LiteralPath (Join-Path $bin "locker.ps1") -Value $locker -Encoding utf8BOM
	$origin = Join-Path $dir "origin.git"
	$root = Join-Path $dir "root"
	Invoke-Git init --quiet --bare -b develop $origin | Out-Null
	Invoke-Git init --quiet -b develop $root | Out-Null
	Copy-Item -LiteralPath $ScriptPath (Join-Path $root "CleanupMerged.ps1")
	Set-Content -LiteralPath (Join-Path $root ".gitignore") -Value "x64/"
	Set-Content -LiteralPath (Join-Path $root "README.md") -Value "test"
	Invoke-Git -C $root add -A | Out-Null
	Invoke-Git -C $root commit --quiet -m "init" | Out-Null
	Invoke-Git -C $root remote add origin $origin | Out-Null
	Invoke-Git -C $root push --quiet -u origin develop | Out-Null
	return [ordered]@{
		Dir = $dir; Bin = $bin; Root = $root; Prs = @{}; Pids = (Join-Path $dir "pids.txt"); OrcaLog = (Join-Path $dir "orca.log")
	}
}

# worktreeとブランチを作り、1つコミットする。-Merge なら develop にマージしてPRをマージ済みにする
function New-Branch($sb, [string]$branch, [switch]$Worktree, [switch]$Merge) {
	$file = "$($branch -replace '/', '-').txt"
	if ($Worktree) {
		$path = Join-Path $sb.Dir "wt-$($branch -replace '/', '-')"
		Invoke-Git -C $sb.Root worktree add --quiet -b $branch $path develop | Out-Null
	}
	else {
		Invoke-Git -C $sb.Root branch $branch develop | Out-Null
		$path = Join-Path $sb.Dir "tmp-$($branch -replace '/', '-')"
		Invoke-Git -C $sb.Root worktree add --quiet $path $branch | Out-Null
	}
	Set-Content -LiteralPath (Join-Path $path $file) -Value $branch
	Invoke-Git -C $path add -A | Out-Null
	Invoke-Git -C $path commit --quiet -m $branch | Out-Null
	if (-not $Worktree) { Invoke-Git -C $sb.Root worktree remove $path | Out-Null; $path = $null }
	if ($Merge) {
		Invoke-Git -C $sb.Root merge --quiet --no-ff -m "Merge $branch" $branch | Out-Null
		Invoke-Git -C $sb.Root push --quiet origin develop | Out-Null
		$sb.Prs[$branch] = 100 + $sb.Prs.Count
	}
	# git worktree list と同じ書き方(/区切り)にそろえる
	if ($path) { return ($path -replace '\\', '/') }
}

# テスト対象を実行する。-NoOrca なら本物も偽物もない PATH にする
function Invoke-Cleanup($sb, [string[]]$arguments = @(), [switch]$NoOrca, [string]$OrcaMode = "async", [string]$Script) {
	$savedPath = $env:PATH
	try {
		$dirs = $env:PATH -split ';' | Where-Object { $_ }
		if ($NoOrca) {
			$orcaDirs = Get-Command orca -All -ErrorAction SilentlyContinue | ForEach-Object { Split-Path $_.Source }
			$dirs = $dirs | Where-Object { $_ -notin $orcaDirs }
			$env:PATH = ($dirs -join ';')
			$gh = Join-Path $sb.Dir "bin-noorca"
			New-Item -ItemType Directory -Force $gh | Out-Null
			Copy-Item -LiteralPath (Join-Path $sb.Bin "gh.ps1") $gh -Force
			$env:PATH = "$gh;$env:PATH"
			if (Get-Command orca -ErrorAction SilentlyContinue) { throw "orca をPATHから外せませんでした。" }
			$expectedGh = Join-Path $gh "gh.ps1"
		}
		else {
			$env:PATH = "$($sb.Bin);$($dirs -join ';')"
			$orca = (Get-Command orca).Source
			if ($orca -ne (Join-Path $sb.Bin "orca.ps1")) { throw "偽の orca ではなく $orca が解決されます。本物を呼ばないよう中止します。" }
			$expectedGh = Join-Path $sb.Bin "gh.ps1"
		}
		$gh = (Get-Command gh).Source
		if ($gh -ne $expectedGh) { throw "偽の gh ではなく $gh が解決されます。本物を呼ばないよう中止します。" }

		$env:DXFORGE_TEST_MERGED_PRS = ($sb.Prs.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ';'
		$env:DXFORGE_TEST_ORCA_MODE = $OrcaMode
		$env:DXFORGE_TEST_ORCA_LOG = $sb.OrcaLog
		$env:DXFORGE_TEST_PIDS = $sb.Pids
		# スクリプトは自分のフォルダ($PSScriptRoot)をリポジトリとして扱うので、ルートに置いて実行する
		if ($Script) {
			Copy-Item -LiteralPath $Script (Join-Path $sb.Root "CleanupMerged.baseline.ps1") -Force
			$Script = Join-Path $sb.Root "CleanupMerged.baseline.ps1"
		}
		else { $Script = Join-Path $sb.Root "CleanupMerged.ps1" }
		$sw = [Diagnostics.Stopwatch]::StartNew()
		$output = & pwsh -NoProfile -File $Script @arguments *>&1 | ForEach-Object { "$_" }
		$exitCode = $LASTEXITCODE
		Write-Host "  --- 出力($([int]$sw.Elapsed.TotalSeconds) 秒、終了コード $exitCode)"
		$output | ForEach-Object { Write-Host "  | $_" }
		return [ordered]@{ Output = @($output); Text = ($output -join "`n"); ExitCode = $exitCode; Seconds = $sw.Elapsed.TotalSeconds }
	}
	finally {
		$env:PATH = $savedPath
	}
}

function Test-BranchExists($sb, [string]$branch) {
	git -C $sb.Root show-ref --verify --quiet "refs/heads/$branch"
	return ($LASTEXITCODE -eq 0)
}

function Test-Registered($sb, [string]$path) {
	return [bool](git -C $sb.Root worktree list --porcelain | Where-Object { $_ -eq "worktree $path" })
}

function Get-OrcaCalls($sb) {
	if (Test-Path -LiteralPath $sb.OrcaLog) { return @(Get-Content -LiteralPath $sb.OrcaLog) }
	return @()
}

# 起動したプロセスを終了させ、作業フォルダを消す
function Remove-Sandbox($sb, [System.Diagnostics.Process[]]$processes = @()) {
	$ids = @($processes | Where-Object { $_ } | ForEach-Object { $_.Id })
	if (Test-Path -LiteralPath $sb.Pids) { $ids += Get-Content -LiteralPath $sb.Pids | ForEach-Object { [int]$_ } }
	foreach ($id in $ids) {
		$proc = Get-Process -Id $id -ErrorAction SilentlyContinue
		if (-not $proc) { continue }
		# 偽の orca の非同期の削除は、終わるまで待つ(途中で止めない)
		if (-not $proc.WaitForExit(10000)) { Stop-Process -Id $id -Force -ErrorAction SilentlyContinue }
		if (-not $proc.WaitForExit(10000)) { $failures.Add("[$currentCase] プロセスを終了できませんでした: PID $id") }
	}
	for ($i = 0; $i -lt 10 -and (Test-Path -LiteralPath $sb.Dir); $i++) {
		Remove-Item -LiteralPath $sb.Dir -Recurse -Force -ErrorAction SilentlyContinue
		if (Test-Path -LiteralPath $sb.Dir) { Start-Sleep -Milliseconds 500 }
	}
	if (Test-Path -LiteralPath $sb.Dir) { $failures.Add("[$currentCase] 作業フォルダを消せませんでした: $($sb.Dir)") }
}

$cases = [ordered]@{}
# pwsh -File では -Case a,b が1つの文字列で渡るので、カンマで分ける
$Case = @($Case | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

$cases["orca-async"] = {
	$sb = New-Sandbox
	try {
		$wt = New-Branch $sb "fix/async" -Worktree -Merge
		$r = Invoke-Cleanup $sb
		Check ($r.ExitCode -eq 0) "終了コードが0"
		Check ((Get-OrcaCalls $sb).Count -eq 1) "偽の orca worktree rm が1回呼ばれた"
		Check ($r.Text -notmatch 'ブランチを削除できませんでした') "「ブランチを削除できませんでした」の警告が出ない"
		Check ($r.Text -notmatch 'WARNING|警告') "警告が1つも出ない"
		Check (-not (Test-BranchExists $sb "fix/async")) "ブランチが消えた"
		Check (-not (Test-Registered $sb $wt)) "worktreeの登録が消えた"
		Check (-not (Test-Path -LiteralPath $wt)) "worktreeのフォルダが消えた"
	}
	finally { Remove-Sandbox $sb }
}

$cases["locked"] = {
	$sb = New-Sandbox
	$child = $null
	try {
		$wt = New-Branch $sb "fix/locked" -Worktree -Merge
		# 無視されたビルド出力の中のDLLを、子プロセスが読み込んだままにする
		$dll = Join-Path ($wt -replace '/', '\') "x64\lockme.dll"
		New-Item -ItemType Directory -Force (Split-Path $dll) | Out-Null
		Copy-Item -LiteralPath (Join-Path $env:SystemRoot "System32\version.dll") $dll
		$ready = Join-Path $sb.Dir "locker-ready.txt"
		$child = Start-Process pwsh -PassThru -WindowStyle Hidden -ArgumentList @(
			"-NoProfile", "-File", (Join-Path $sb.Bin "locker.ps1"), $dll, $ready)
		for ($i = 0; $i -lt 60 -and -not (Test-Path -LiteralPath $ready); $i++) { Start-Sleep -Milliseconds 500 }
		if (-not (Test-Path -LiteralPath $ready)) { throw "子プロセスがDLLを読み込めませんでした。" }

		$r = Invoke-Cleanup $sb
		Check ($r.ExitCode -eq 0) "終了コードが0"
		Check ($r.Text -match 'worktreeのフォルダが残っています') "フォルダが残った警告が出る"
		Check ($r.Text -match "PID $($child.Id)\b") "警告に子プロセスのPID $($child.Id) が出る"
		Check ($r.Text -notmatch 'ブランチを削除できませんでした') "「ブランチを削除できませんでした」の警告が出ない"
		Check (Test-Path -LiteralPath $dll) "ロックされたDLLは残っている(スクリプトはプロセスを終了させない)"
		Check (-not $child.HasExited) "子プロセスは動いたまま"
		Check (-not (Test-BranchExists $sb "fix/locked")) "ブランチが消えた"
		Check (-not (Test-Registered $sb ($wt))) "worktreeの登録が消えた"
	}
	finally { Remove-Sandbox $sb @($child) }
}

$cases["orca-keep"] = {
	$sb = New-Sandbox
	try {
		$wt = New-Branch $sb "fix/keep" -Worktree -Merge
		$r = Invoke-Cleanup $sb -OrcaMode keep
		Check ($r.ExitCode -eq 0) "終了コードが0"
		Check ($r.Text -match 'Orcaの削除が終わりませんでした') "登録が残った警告が出る"
		Check ($r.Text -notmatch '(?m)^削除: fix/keep') "「ローカルブランチ」の節でブランチを消そうとしない"
		Check ($r.Text -notmatch 'ブランチを削除できませんでした') "「ブランチを削除できませんでした」の警告が出ない"
		Check (Test-BranchExists $sb "fix/keep") "ブランチは残る"
		Check (Test-Registered $sb $wt) "worktreeの登録は残る"
		Check ($r.Seconds -ge 14) "上限(15秒)まで待った($([int]$r.Seconds) 秒)"
	}
	finally { Remove-Sandbox $sb }
}

$cases["git-only"] = {
	$sb = New-Sandbox
	try {
		$wt = New-Branch $sb "fix/git" -Worktree -Merge
		New-Branch $sb "fix/plain" -Merge
		$r = Invoke-Cleanup $sb -NoOrca
		Check ($r.ExitCode -eq 0) "終了コードが0"
		Check ((Get-OrcaCalls $sb).Count -eq 0) "orca は呼ばれない"
		Check ($r.Text -notmatch 'WARNING|警告') "警告が1つも出ない"
		Check (-not (Test-BranchExists $sb "fix/git")) "worktreeのブランチが消えた"
		Check (-not (Test-BranchExists $sb "fix/plain")) "worktreeのないブランチが消えた"
		Check (-not (Test-Registered $sb $wt)) "worktreeの登録が消えた"
		Check (-not (Test-Path -LiteralPath $wt)) "worktreeのフォルダが消えた"
	}
	finally { Remove-Sandbox $sb }
}

$cases["keep-unsafe"] = {
	$sb = New-Sandbox
	try {
		$unmerged = New-Branch $sb "fix/unmerged" -Worktree
		$ahead = New-Branch $sb "fix/ahead" -Worktree -Merge
		Set-Content -LiteralPath (Join-Path $ahead "more.txt") -Value "more"
		Invoke-Git -C $ahead add -A | Out-Null
		Invoke-Git -C $ahead commit --quiet -m "more" | Out-Null
		$dirty = New-Branch $sb "fix/dirty" -Worktree -Merge
		Add-Content -LiteralPath (Join-Path $dirty "fix-dirty.txt") -Value "changed"
		$untracked = New-Branch $sb "fix/untracked" -Worktree -Merge
		Set-Content -LiteralPath (Join-Path $untracked "new.txt") -Value "new"
		New-Branch $sb "fix/plain-unmerged"
		$r = Invoke-Cleanup $sb
		Check ($r.ExitCode -eq 0) "終了コードが0"
		Check ((Get-OrcaCalls $sb).Count -eq 0) "orca worktree rm は呼ばれない"
		Check ($r.Text -match '残す: .*\[fix/unmerged\].*マージ済みのPRがない') "未マージのworktreeを残すと表示する"
		Check ($r.Text -match '\[fix/ahead\].*マージ後に足したコミットがある') "マージ後のコミットがあるworktreeを残すと表示する"
		Check ($r.Text -match '\[fix/dirty\].*未コミットの変更か追跡されていないファイルがある') "未コミットの変更があるworktreeを残すと表示する"
		Check ($r.Text -match '\[fix/untracked\].*未コミットの変更か追跡されていないファイルがある') "未追跡ファイルがあるworktreeを残すと表示する"
		Check ($r.Text -match '残す: fix/plain-unmerged') "未マージのブランチを残すと表示する"
		foreach ($path in $unmerged, $ahead, $dirty, $untracked) {
			Check ((Test-Registered $sb $path) -and (Test-Path -LiteralPath $path)) "残っている: $path"
		}
		foreach ($branch in "fix/unmerged", "fix/ahead", "fix/dirty", "fix/untracked", "fix/plain-unmerged") {
			Check (Test-BranchExists $sb $branch) "ブランチが残っている: $branch"
		}
		Check ($r.Seconds -lt 10) "消すものがないので待たない($([int]$r.Seconds) 秒)"
	}
	finally { Remove-Sandbox $sb }
}

$cases["dry-run"] = {
	$sb = New-Sandbox
	try {
		$wt = New-Branch $sb "fix/dry" -Worktree -Merge
		New-Branch $sb "fix/dry-plain" -Merge
		$r = Invoke-Cleanup $sb -arguments "-DryRun"
		Check ($r.ExitCode -eq 0) "終了コードが0"
		Check ((Get-OrcaCalls $sb).Count -eq 0) "orca は呼ばれない"
		Check ($r.Text -match '\(DryRun\) git worktree remove') "worktreeの削除を表示するだけ"
		Check ($r.Text -match '\(DryRun\) git branch -D fix/dry-plain') "ブランチの削除を表示するだけ"
		Check ($r.Text -notmatch 'WARNING|警告') "警告が出ない(待ちもプロセスの走査もしない)"
		Check ((Test-Registered $sb $wt) -and (Test-Path -LiteralPath $wt)) "worktreeは残っている"
		Check ((Test-BranchExists $sb "fix/dry") -and (Test-BranchExists $sb "fix/dry-plain")) "ブランチは残っている"
		Check ($r.Seconds -lt 10) "待たない($([int]$r.Seconds) 秒)"
		if ($BaselineScriptPath) {
			$b = Invoke-Cleanup $sb -arguments "-DryRun" -Script $BaselineScriptPath
			Check (($b.Text -replace '\\', '/') -eq ($r.Text -replace '\\', '/')) "-DryRun の出力が $BaselineScriptPath と同じ"
		}
	}
	finally { Remove-Sandbox $sb }
}

foreach ($name in $Case) {
	if ($name -notin $cases.Keys) { $failures.Add("ケースがありません: $name") }
}
foreach ($name in $cases.Keys) {
	if ($Case -and $name -notin $Case) { continue }
	$currentCase = $name
	Write-Host "== $name"
	try { & $cases[$name] }
	catch {
		Write-Host "  NG: $_" -ForegroundColor Red
		$failures.Add("[$name] $_")
	}
}

Write-Host ""
if ($failures.Count) {
	Write-Host "失敗: $($failures.Count) 件" -ForegroundColor Red
	$failures | ForEach-Object { Write-Host "  $_" }
	exit 1
}
Write-Host "すべて成功しました。"
exit 0
