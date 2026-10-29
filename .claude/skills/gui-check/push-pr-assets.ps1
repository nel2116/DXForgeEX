<#
.SYNOPSIS
	GUI 確認のスクリーンショットを pr-assets ブランチに push し、PR の本文に画像として載せるスクリプト。

.DESCRIPTION
	gh CLI では PR に画像を添付できないので、origin の orphan ブランチ pr-assets に PNG を置き、
	コミットの SHA で固定した URL を PR の本文に埋め込む。作業中のブランチと worktree には触らない
	(一時的なインデックスファイルと git commit-tree で pr-assets のコミットを作る)。

	-Dir の PNG を、名前で次のように並べる。
	- base-<名前>.png と pr-<名前>.png の組: 「変更前 | この PR」の比較表の 1 行(<名前> が行の見出し)
	- それ以外の PNG: 比較表の下に 1 枚ずつ

	PR の本文の <!-- gui-check:start --> ～ <!-- gui-check:end --> の部分を置き換える(なければ、
	本文の Refs/Closes の行の前に足す)。同じ PR でやり直しても、節は 1 つのままになる。

.PARAMETER Pr
	PR の番号。

.PARAMETER Dir
	PNG を置いたフォルダ。

.PARAMETER Summary
	節の先頭に書く本文(Markdown のファイル)。確認の日付、比べた版、手順ごとの結果を書く。

.PARAMETER BaseLabel
	比較表の「変更前」の列の見出し。既定値: develop。

.PARAMETER DryRun
	push と PR の編集をせず、作る節を表示するだけにする。

.EXAMPLE
	./.claude/skills/gui-check/push-pr-assets.ps1 -Pr 128 -Dir $env:TEMP\gui128 -Summary $env:TEMP\gui128.md -BaseLabel "develop(3fd1761)"
#>
param(
	[Parameter(Mandatory)][int]$Pr,
	[Parameter(Mandatory)][string]$Dir,
	[Parameter(Mandatory)][string]$Summary,
	[string]$BaseLabel = "develop",
	[switch]$DryRun
)

$ErrorActionPreference = "Stop"
# gh の出力(日本語の PR 本文)を UTF-8 として受け取る。Windows PowerShell 5.1 の既定(CP932)だと文字化けしたまま保存し直してしまう。
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8
$OutputEncoding = [System.Text.Encoding]::UTF8
$startMarker = "<!-- gui-check:start -->"
$endMarker = "<!-- gui-check:end -->"
$utf8 = [System.Text.UTF8Encoding]::new($false)

# .NET の API はプロセスの作業フォルダを基準にするので、PowerShell の現在位置を基準に絶対パスにしておく。
# 本文は push の前に読む(読めないときに pr-assets だけ更新されないように)。
$Dir = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Dir)
$Summary = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Summary)
$summaryText = [System.IO.File]::ReadAllText($Summary, $utf8).TrimEnd()
$pngs = @(Get-ChildItem -LiteralPath $Dir -Filter *.png | Sort-Object Name)
if ($pngs.Count -eq 0) { throw "PNG がありません: $Dir" }
# 現在位置がリポジトリの外でも動くように、スクリプトの置き場所からリポジトリを決め、gh にもリポジトリを明示する
$repo = (git -C $PSScriptRoot rev-parse --show-toplevel).Trim()
$ghRepo = "nel2116/DXForgeEX"
$prHead = gh pr view $Pr --repo $ghRepo --json headRefOid --jq .headRefOid
if ($LASTEXITCODE -ne 0 -or -not $prHead) { throw "PR #$Pr を取得できませんでした" }
$prHead = $prHead.Trim()
$folder = "pr-$Pr/gui-check"

# ---- pr-assets にコミットする(作業中のインデックスとブランチには触らない)----
$commit = "<dry-run>"
if (-not $DryRun)
{
	git -C $repo fetch -q origin pr-assets
	$parent = (git -C $repo rev-parse origin/pr-assets).Trim()
	$indexFile = Join-Path $env:TEMP "pr-assets-index-$PID"
	$env:GIT_INDEX_FILE = $indexFile
	try
	{
		git -C $repo read-tree $parent
		foreach ($png in $pngs)
		{
			$blob = (git -C $repo hash-object -w -- $png.FullName).Trim()
			git -C $repo update-index --add --cacheinfo "100644,$blob,$folder/$($png.Name)"
		}
		$tree = (git -C $repo write-tree).Trim()
	}
	finally
	{
		Remove-Item Env:\GIT_INDEX_FILE
		Remove-Item -LiteralPath $indexFile -ErrorAction SilentlyContinue
	}
	# 同じ画像を載せ直すときは、空のコミットを作らずに今のコミットを使う
	if ($tree -eq (git -C $repo rev-parse "$parent^{tree}").Trim())
	{
		$commit = $parent
	}
	else
	{
		$commit = (git -C $repo commit-tree $tree -p $parent -m "[chore] PR #$Pr の GUI 確認のスクリーンショット").Trim()
		git -C $repo push -q origin "${commit}:refs/heads/pr-assets"
		if ($LASTEXITCODE -ne 0) { throw "pr-assets への push に失敗しました" }
	}
}

function Get-ImageUrl([string]$Name)
{
	# 空白などを含むファイル名でも Markdown の画像リンクとして有効になるよう、パス要素としてエンコードする
	return "https://github.com/nel2116/DXForgeEX/blob/$commit/$folder/$([System.Uri]::EscapeDataString($Name))`?raw=true"
}

# ---- 節を組み立てる ----
$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add($startMarker)
$lines.Add($summaryText)
$lines.Add("")
$lines.Add("スクリーンショット(PR の $($prHead.Substring(0, 7)) で撮影、pr-assets の ``$folder/``):")
$lines.Add("")

$used = @{}
$pairs = @($pngs | Where-Object { $_.Name -like "pr-*.png" } | ForEach-Object {
	$key = $_.Name.Substring(3)
	$base = $pngs | Where-Object { $_.Name -eq "base-$key" }
	if ($base) { [pscustomobject]@{ Key = [System.IO.Path]::GetFileNameWithoutExtension($key); Base = $base.Name; Pr = $_.Name } }
})
if ($pairs.Count -gt 0)
{
	$lines.Add("| | $BaseLabel | この PR |")
	$lines.Add("|---|---|---|")
	foreach ($pair in $pairs)
	{
		$lines.Add("| $($pair.Key) | ![]($(Get-ImageUrl $pair.Base)) | ![]($(Get-ImageUrl $pair.Pr)) |")
		$used[$pair.Base] = $true
		$used[$pair.Pr] = $true
	}
	$lines.Add("")
}
foreach ($png in $pngs | Where-Object { -not $used.ContainsKey($_.Name) })
{
	$lines.Add("$([System.IO.Path]::GetFileNameWithoutExtension($png.Name)):")
	$lines.Add("")
	$lines.Add("![]($(Get-ImageUrl $png.Name))")
	$lines.Add("")
}
$lines.Add($endMarker)
$section = $lines -join "`n"

if ($DryRun)
{
	$section
	return
}

# ---- 画像の URL が見えることを確かめる(push 直後は反映が遅れることがある)----
foreach ($png in $pngs)
{
	$url = Get-ImageUrl $png.Name
	$status = 0
	for ($i = 0; $i -lt 5 -and $status -ne 200; $i++)
	{
		if ($i -gt 0) { Start-Sleep -Seconds 3 }
		try { $status = (Invoke-WebRequest -Uri $url -Method Head -UseBasicParsing).StatusCode } catch { $status = 0 }
	}
	if ($status -ne 200) { throw "画像の URL が 200 を返しません: $url" }
}

# ---- PR の本文の節を置き換える(なければ Refs/Closes の行の前に足す)----
$bodyLines = gh pr view $Pr --repo $ghRepo --json body --jq .body
# 取得に失敗したまま進むと、既存の本文をこの節だけで上書きしてしまう
if ($LASTEXITCODE -ne 0) { throw "PR #$Pr の本文を取得できませんでした(本文は変更していません)" }
$body = $bodyLines -join "`n"
$start = $body.IndexOf($startMarker)
$end = $body.IndexOf($endMarker)
if ($start -ge 0 -and $end -gt $start)
{
	$body = $body.Substring(0, $start) + $section + $body.Substring($end + $endMarker.Length)
}
else
{
	$match = [regex]::Match($body, "(?m)^(Refs|Closes|Fixes) #\d+")
	if ($match.Success) { $body = $body.Substring(0, $match.Index) + $section + "`n`n" + $body.Substring($match.Index) }
	else { $body = $body.TrimEnd() + "`n`n" + $section + "`n" }
}
$bodyFile = Join-Path $env:TEMP "pr-$Pr-body-$PID.md"
try
{
	[System.IO.File]::WriteAllText($bodyFile, $body, $utf8)
	gh pr edit $Pr --repo $ghRepo --body-file $bodyFile | Out-Null
	if ($LASTEXITCODE -ne 0) { throw "PR の本文を更新できませんでした" }
}
finally
{
	Remove-Item -LiteralPath $bodyFile -ErrorAction SilentlyContinue
}
"pr-assets: $commit"
"PR #$Pr の本文に $($pngs.Count) 枚の画像を載せました: https://github.com/nel2116/DXForgeEX/pull/$Pr"
