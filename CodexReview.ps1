<#
.SYNOPSIS
	PRの変更を Codex CLI (codex review) でレビューし、結果を表示する(-Post でPRにコメントする)スクリプト。

.DESCRIPTION
	実装したエージェントとは別のモデルに差分を見せ、別の視点からの指摘を得るためのもの。
	PRのhead(refs/pull/<番号>/head)を一時フォルダに使い捨てのworktreeとして展開し、その中で
	codex review を実行する。終わったら(失敗しても)worktreeは削除する。作業中のworktreeには触らない。

	注意点:
	- Windowsでは Codex のサンドボックスが無効("Windows sandbox selection: disabled")だと、読み取り専用の
	  レビューでもコマンドの実行がすべて "blocked by policy" で拒否され、差分を読めないまま終わる。
	  そのため呼び出しごとに -c windows.sandbox="unelevated" を渡す(~/.codex/config.toml は変更しない)。
	- codex review は --base とカスタム指示を同時に指定できないため、差分の範囲は指示の中で指定する。
	- マージ済みのPRは、マージコミットの2つの親(マージ直前のbaseとPRのhead)を比べ、各指摘が今のbaseでも
	  残っているかもCodexに確かめさせる(merge commitでマージされたPRのみ対応)。
	- 所要時間の目安は1PRあたり1〜数分。ビルドやテストは行わない(読み取り専用)ため、CIの代わりにはならない。

.PARAMETER Pr
	レビューするPRの番号。

.PARAMETER Post
	レビュー結果をPRにコメントとして投稿する。指定しない場合は表示するだけ。

.PARAMETER Effort
	Codexの推論の強さ(model_reasoning_effort)。既定値: high。

.PARAMETER SetStatus
	マージの関門として、レビューしたコミットにコミットステータス codex-review を付ける。
	Codexの出力に指摘("- [P0〜P3] ..." の行)がなければ success を付けて自動マージ(gh pr merge --auto --merge)を
	予約し、指摘があれば重大度を問わず failure を付ける。向き先のブランチ保護が codex-review と No hold label を
	必須にしていないPRには使えない(レビューの前に確かめて止まる)。developのブランチ保護はCI・hold・
	codex-review の成功を必須にしているため、残りがそろった時点でGitHubがマージする。
	ステータスはコミットに付くので、修正をpushすると外れ、再レビューで success になるまでマージされない。
	PRの完了条件(GUIでの確認など)がそろってから、コーディネーターが指定すること。マージ済みのPRには使えない。

.EXAMPLE
	./CodexReview.ps1 -Pr 52
	PR #52 をレビューして結果を表示する。

.EXAMPLE
	./CodexReview.ps1 -Pr 52 -Post
	PR #52 をレビューして結果をPRにコメントする。

.EXAMPLE
	./CodexReview.ps1 -Pr 52 -Post -SetStatus
	PR #52 をレビューしてコメントし、指摘がなければ codex-review を success にして自動マージを予約する。
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[int]$Pr,

	[switch]$Post,

	[ValidateSet("low", "medium", "high", "xhigh")]
	[string]$Effort = "high",

	[switch]$SetStatus
)

$ErrorActionPreference = "Stop"
$RepoRoot = $PSScriptRoot

foreach ($tool in "git", "gh", "codex") {
	if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
		throw "$tool が見つかりません。"
	}
}

# Codexの出力(日本語)を文字化けさせずに受け取る
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8

# gh は呼び出し元のディレクトリで対象リポジトリを決めるため、このリポジトリの中で実行する
Push-Location $RepoRoot
try {
	$info = gh pr view $Pr --json number,title,url,state,baseRefName,headRefOid,mergeCommit | ConvertFrom-Json
	$repoUrl = gh repo view --json url --jq .url
}
finally {
	Pop-Location
}
if (-not $info -or -not $repoUrl) { throw "PR #$Pr の情報を取得できませんでした。" }
$base = $info.baseRefName
$head = $info.headRefOid

# originの設定(fork、single-branch clone など)に左右されないよう、ghが選んだリポジトリから
# baseとPRのheadを、このスクリプト専用のrefに取得先を明示して取得する
# 同じPRを並行してレビューしても衝突しないよう、refとworktreeの名前には実行ごとのIDを含める
$runId = [guid]::NewGuid().ToString('N').Substring(0, 8)
$ref = "refs/codex-review/$Pr-$runId"
# FETCH_HEADは共有されるため、並行して実行したときに書き込みが競合しないよう書き出さない
git -C $RepoRoot fetch --quiet --no-tags --no-write-fetch-head $repoUrl "+refs/heads/${base}:$ref/base" "+refs/pull/$Pr/head:$ref/head"
if ($LASTEXITCODE -ne 0) { throw "git fetch に失敗しました。" }
$baseSha = git -C $RepoRoot rev-parse "$ref/base"
# gh で情報を取得してから push されていても、実際に取得したheadをレビューする
$head = git -C $RepoRoot rev-parse "$ref/head"

# マージ済みのPRは、今のbaseにPR自身の変更が含まれていて差分が空になるため、
# マージコミットの1番目の親(マージ直前のbase)と2番目の親(マージ時点のPRのhead)を比べる
$merged = $info.state -eq "MERGED"
if ($SetStatus -and $info.state -ne "OPEN") {
	git -C $RepoRoot update-ref -d "$ref/base"
	git -C $RepoRoot update-ref -d "$ref/head"
	throw "-SetStatus はオープンなPRにしか使えません(PR #$Pr は $($info.state))。"
}
if ($SetStatus) {
	# gh pr merge --auto は必須のチェックがないブランチでは即座にマージするため、向き先のブランチ保護が
	# codex-review と hold のチェックを必須にしていることを確かめてから進む(レビューの前に確かめる)
	Push-Location $RepoRoot
	try {
		$required = @(gh api "repos/{owner}/{repo}/branches/$base/protection/required_status_checks" --jq '.contexts[]' 2>$null)
	}
	finally {
		Pop-Location
	}
	if ($LASTEXITCODE -ne 0 -or "codex-review" -notin $required -or "No hold label" -notin $required) {
		git -C $RepoRoot update-ref -d "$ref/base"
		git -C $RepoRoot update-ref -d "$ref/head"
		throw "向き先 $base のブランチ保護が codex-review と No hold label を必須にしていないため、-SetStatus(自動マージ)は使えません。"
	}
}
$currentSha = $baseSha
if ($merged) {
	$mergeSha = $info.mergeCommit.oid
	$parents = @(git -C $RepoRoot rev-list --parents -n 1 $mergeSha) -split " "
	if ($parents.Count -ne 3) {
		git -C $RepoRoot update-ref -d "$ref/base"
		git -C $RepoRoot update-ref -d "$ref/head"
		throw "マージコミット $mergeSha の親が2つではありません(squashやrebaseでマージされたPRには未対応です)。"
	}
	$baseSha = $parents[1]
	$head = $parents[2]
}

$work = Join-Path ([System.IO.Path]::GetTempPath()) "DXForgeCodexReview-$Pr-$runId"
$log = "$work.log"
git -C $RepoRoot worktree add --quiet --detach $work $head
if ($LASTEXITCODE -ne 0) {
	git -C $RepoRoot update-ref -d "$ref/base"
	git -C $RepoRoot update-ref -d "$ref/head"
	throw "一時worktreeの作成に失敗しました。"
}

$baseNote = if ($merged) { "マージ直前の $base" } else { "PRの向き先 $base の先端" }
$prompt = @"
このリポジトリは、PR #$Pr「$($info.title)」のheadをチェックアウトしたものです。``git diff $baseSha...HEAD`` (``$baseSha`` は$baseNote)の変更をレビューしてください。
- ファイルは変更せず、ビルドやテストも実行しないこと。
- 文字コード(BOM)や改行コードだけの変更は指摘しなくてよい(BOMが欠けている場合は指摘すること)。
"@
if ($merged) {
	$prompt += @"

- このPRはすでに $base にマージされている。各指摘について、今の $base の先端(``$currentSha``)でもまだ起きるかを ``git show ${currentSha}:<パス>`` などで確かめ、「現在も残っている」か「その後修正済み」かを必ず書くこと。今の規約は ``git show ${currentSha}:CLAUDE.md`` で読める。
"@
}
$prompt += "`n" + @"

- 指摘は日本語で書き、それぞれに重大度(P0〜P3)、ファイル:行、その不具合が起きる具体的な手順や入力を書くこと。推測で書いた指摘はそうと分かるようにすること。
- 変更された行だけでなく、呼び出し元や関連する箇所も読んで、変更が既存の挙動を壊していないかを確かめること。
- リポジトリのCLAUDE.mdの規約も確認すること(特に: C++/C#のファイルはUTF-8 BOM、日本語ヘッダの更新履歴は追記のみ、狭い文字列リテラルに日本語を入れない、async void・OpenProjectViewから呼ばれる経路で例外を投げない、警告を握りつぶさない)。
- 問題がなければ、そう書くこと。
"@

try {
	Push-Location $work
	try {
		$review = codex review -c 'windows.sandbox="unelevated"' -c "model_reasoning_effort=`"$Effort`"" $prompt 2> $log
		$codexExit = $LASTEXITCODE
	}
	finally {
		Pop-Location
	}

	$logText = if (Test-Path $log) { Get-Content -Raw -Encoding utf8 $log } else { "" }
	$review = ($review -join "`n").Trim()
	if ($codexExit -ne 0 -or -not $review) {
		throw "codex review が失敗しました(終了コード $codexExit)。ログ: $log"
	}
	# ログにはCodexが読んだファイルの内容も残るため、文字列が含まれるだけでは判定せず、
	# 実際にコマンドの実行が拒否されたときのエラー行だけを見る
	if ($logText -match "(?m)^\S+\s+ERROR codex_core::tools::router: .*blocked by policy") {
		throw "Codexがコマンドを実行できず、差分を読めていない可能性があります(blocked by policy)。ログ: $log"
	}

	# 一時worktreeの絶対パスをリポジトリからの相対パスに直す
	foreach ($prefix in ($work + "\"), ($work.Replace("\", "/") + "/")) {
		$review = $review.Replace($prefix, "")
	}
	$model = if ($logText -match "(?m)^model:\s*(\S+)") { $Matches[1] } else { "不明" }
}
finally {
	git -C $RepoRoot worktree remove --force $work 2>$null
	if ($LASTEXITCODE -ne 0) { Write-Warning "一時worktreeを削除できませんでした: $work" }
	git -C $RepoRoot update-ref -d "$ref/base"
	git -C $RepoRoot update-ref -d "$ref/head"
}

Remove-Item -ErrorAction SilentlyContinue $log
Write-Output $review

# codex review は独自の出力形式に従い、カスタム指示で最後の行の書式を指定しても守らない。
# 指摘は必ず "- [P1] 見出し — ファイル:行" の形の行で出る("Full review comments:" の下)ので、
# その行が1つでもあれば指摘あり(重大度を問わない)、なければ指摘なしとする
$verdict = if ($review -match '(?m)^\s*-\s*\[P[0-3]\]' -or $review -match '(?m)^Full review comments:') { "FINDINGS" } else { "PASS" }
Write-Output "判定: $verdict"

$commentUrl = $null
if ($Post) {
	$target = if ($merged) {
		"マージ済み。$($head.Substring(0, 7))(マージ直前の ``$base`` の $($baseSha.Substring(0, 7)) との差分)、現在の ``$base`` は $($currentSha.Substring(0, 7))"
	}
	else {
		"$($head.Substring(0, 7))(``$base`` の $($baseSha.Substring(0, 7)) との差分)"
	}
	$body = @"
## Codex レビュー

対象: $target / モデル: $model / 推論: $Effort

$review

---
``CodexReview.ps1`` による自動レビューです。ビルドやテストは行っていません。指摘は、修正するか、修正しない理由を返信してください。
"@
	$bodyFile = [System.IO.Path]::GetTempFileName()
	Push-Location $RepoRoot
	try {
		[System.IO.File]::WriteAllText($bodyFile, $body, [System.Text.UTF8Encoding]::new($false))
		$commentUrl = gh pr comment $Pr --body-file $bodyFile
		if ($LASTEXITCODE -ne 0) { throw "PRへのコメントに失敗しました。" }
		Write-Output $commentUrl
	}
	finally {
		Pop-Location
		Remove-Item -ErrorAction SilentlyContinue $bodyFile
	}
}

if ($SetStatus) {
	$state = if ($verdict -eq "PASS") { "success" } else { "failure" }
	$description = if ($verdict -eq "PASS") { "Codex review: no findings ($model, $Effort)" }
		else { "Codex review: findings to address ($model, $Effort)" }
	Push-Location $RepoRoot
	try {
		# レビューしたコミット($head)に付ける。レビュー中にpushされていても、新しいheadは未レビューのまま残る
		$statusArgs = @("api", "--silent", "repos/{owner}/{repo}/statuses/$head",
			"-f", "state=$state", "-f", "context=codex-review", "-f", "description=$description")
		if ($commentUrl) { $statusArgs += @("-f", "target_url=$commentUrl") }
		gh @statusArgs
		if ($LASTEXITCODE -ne 0) { throw "コミットステータスの設定に失敗しました。" }
		Write-Output "codex-review: $state ($($head.Substring(0, 7)))"

		if ($verdict -eq "PASS") {
			$nowHead = gh pr view $Pr --json headRefOid --jq .headRefOid
			if ($nowHead -ne $head) {
				Write-Warning "レビュー中にPRのheadが $($nowHead.Substring(0, 7)) に進みました。新しいheadは未レビューのため、自動マージは予約しません。"
			}
			else {
				# 必須のチェックがそろっていれば即座に、そうでなければそろった時点でGitHubがマージする
				gh pr merge $Pr --auto --merge
				if ($LASTEXITCODE -ne 0) { throw "自動マージの予約に失敗しました。" }
				Write-Output "自動マージを予約しました(CI・hold・codex-review がそろうとマージされます)。"
			}
		}
	}
	finally {
		Pop-Location
	}
}
