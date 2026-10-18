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
	- 所要時間の目安は1PRあたり1〜数分。ビルドやテストは行わない(読み取り専用)ため、CIの代わりにはならない。

.PARAMETER Pr
	レビューするPRの番号。

.PARAMETER Post
	レビュー結果をPRにコメントとして投稿する。指定しない場合は表示するだけ。

.PARAMETER Effort
	Codexの推論の強さ(model_reasoning_effort)。既定値: high。

.EXAMPLE
	./CodexReview.ps1 -Pr 52
	PR #52 をレビューして結果を表示する。

.EXAMPLE
	./CodexReview.ps1 -Pr 52 -Post
	PR #52 をレビューして結果をPRにコメントする。
#>
[CmdletBinding()]
param(
	[Parameter(Mandatory = $true)]
	[int]$Pr,

	[switch]$Post,

	[ValidateSet("low", "medium", "high", "xhigh")]
	[string]$Effort = "high"
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
	$info = gh pr view $Pr --json number,title,url,baseRefName,headRefOid | ConvertFrom-Json
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
git -C $RepoRoot fetch --quiet --no-tags $repoUrl "+refs/heads/${base}:$ref/base" "+refs/pull/$Pr/head:$ref/head"
if ($LASTEXITCODE -ne 0) { throw "git fetch に失敗しました。" }
$baseSha = git -C $RepoRoot rev-parse "$ref/base"
# gh で情報を取得してから push されていても、実際に取得したheadをレビューする
$head = git -C $RepoRoot rev-parse "$ref/head"

$work = Join-Path ([System.IO.Path]::GetTempPath()) "DXForgeCodexReview-$Pr-$runId"
$log = "$work.log"
git -C $RepoRoot worktree add --quiet --detach $work $head
if ($LASTEXITCODE -ne 0) {
	git -C $RepoRoot update-ref -d "$ref/base"
	git -C $RepoRoot update-ref -d "$ref/head"
	throw "一時worktreeの作成に失敗しました。"
}

$prompt = @"
このリポジトリは、PR #$Pr「$($info.title)」のheadをチェックアウトしたものです。``git diff $baseSha...HEAD`` (``$baseSha`` はPRの向き先 $base の先端)の変更をレビューしてください。
- ファイルは変更せず、ビルドやテストも実行しないこと。
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

if ($Post) {
	$body = @"
## Codex レビュー

対象: $($head.Substring(0, 7))(``$base`` の $($baseSha.Substring(0, 7)) との差分) / モデル: $model / 推論: $Effort

$review

---
``CodexReview.ps1`` による自動レビューです。ビルドやテストは行っていません。指摘は、修正するか、修正しない理由を返信してください。
"@
	$bodyFile = [System.IO.Path]::GetTempFileName()
	Push-Location $RepoRoot
	try {
		[System.IO.File]::WriteAllText($bodyFile, $body, [System.Text.UTF8Encoding]::new($false))
		gh pr comment $Pr --body-file $bodyFile
		if ($LASTEXITCODE -ne 0) { throw "PRへのコメントに失敗しました。" }
	}
	finally {
		Pop-Location
		Remove-Item -ErrorAction SilentlyContinue $bodyFile
	}
}
