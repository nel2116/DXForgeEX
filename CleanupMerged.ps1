<#
.SYNOPSIS
	マージ済みのPRのworktreeとローカルブランチを片付け、ルートのdevelopを最新にするスクリプト。

.DESCRIPTION
	PRがマージされたあとの手元の片付けをまとめて行う。リモートのブランチはGitHubの設定
	(delete_branch_on_merge)でマージ時に消え、Issueは .github/workflows/pr-automation.yml が閉じるので、
	ここでは手元の状態だけを扱う。

	消すのは、次をすべて満たすものだけ。満たさないものは理由を表示して残す。
	- ブランチに対応するPRがマージ済み(gh pr list --state merged --head <ブランチ>)
	- ブランチの先端が origin/develop に含まれている(マージ後に足したコミットがない)
	- worktreeに未コミットの変更や追跡されていないファイルがない(無視されたビルド成果物は除く)
	起点にしただけでまだコミットのない作業中のworktreeは、マージ済みのPRがないので消さない。

	Orcaが管理するworktreeは orca worktree rm で消す(Orcaの記録とブランチも一緒に消える)。
	それ以外、またはOrcaで消せなかったときは git worktree remove と git branch -D を使う。

	最後に、ルートのチェックアウトが develop で、追跡しているファイルに変更がなければ
	git pull --ff-only する。

.PARAMETER DryRun
	何を消すか(残すか)を表示するだけで、変更はしない。

.EXAMPLE
	./CleanupMerged.ps1 -DryRun
	片付けの対象を確かめる。

.EXAMPLE
	./CleanupMerged.ps1
	マージ済みのworktreeとブランチを消し、developを最新にする。
#>
[CmdletBinding()]
param(
	[switch]$DryRun
)

$ErrorActionPreference = "Stop"
[Console]::OutputEncoding = [System.Text.Encoding]::UTF8

foreach ($tool in "git", "gh") {
	if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
		throw "$tool が見つかりません。"
	}
}
$hasOrca = [bool](Get-Command orca -ErrorAction SilentlyContinue)

# git worktree list の先頭はメインのworktree(ルートのチェックアウト)。
# このスクリプトがworktreeの中から実行されても、ルートを基準にする
$porcelain = git -C $PSScriptRoot worktree list --porcelain
if ($LASTEXITCODE -ne 0) { throw "git worktree list に失敗しました。" }
$worktrees = @()
$current = $null
foreach ($line in $porcelain) {
	if ($line -match '^worktree (.+)$') {
		$current = [ordered]@{ Path = $Matches[1]; Branch = $null; Prunable = $false }
		$worktrees += $current
	}
	elseif ($line -match '^branch refs/heads/(.+)$') { $current.Branch = $Matches[1] }
	elseif ($line -match '^prunable') { $current.Prunable = $true }
}
$root = $worktrees[0].Path

function Invoke-Step([string]$description, [scriptblock]$action) {
	if ($DryRun) {
		Write-Host "  (DryRun) $description"
		return $true
	}
	# コマンドの出力が戻り値に混ざらないよう、画面にだけ出す
	& $action | ForEach-Object { Write-Host "  $_" }
	return ($LASTEXITCODE -eq 0)
}

Push-Location $root
try {
	git fetch --quiet --prune origin
	if ($LASTEXITCODE -ne 0) { throw "git fetch に失敗しました。" }
	if (-not $DryRun) { git worktree prune }

	# マージ済みかどうかはブランチ名でPRを引いて判定する(同じ名前で何度もPRを作ることはない前提)
	$mergedCache = @{}
	function Get-MergedPr([string]$branch) {
		if (-not $mergedCache.ContainsKey($branch)) {
			$pr = gh pr list --state merged --head $branch --limit 1 --json number --jq '.[0].number'
			$mergedCache[$branch] = if ($LASTEXITCODE -eq 0 -and $pr) { [int]$pr } else { $null }
		}
		return $mergedCache[$branch]
	}

	function Test-Removable([string]$branch, [string]$path) {
		if ($branch -in "develop", "main") { return "保護するブランチ" }
		$pr = Get-MergedPr $branch
		if (-not $pr) { return "マージ済みのPRがない" }
		git merge-base --is-ancestor "refs/heads/$branch" origin/develop
		if ($LASTEXITCODE -ne 0) { return "PR #$pr のマージ後に足したコミットがある" }
		if ($path) {
			# status.showUntrackedFiles などの設定に左右されないよう、追跡されていないファイルの表示を明示する
			$dirty = git -C $path status --porcelain --untracked-files=all
			if ($LASTEXITCODE -ne 0) { return "worktreeの状態を確認できない" }
			if ($dirty) { return "未コミットの変更か追跡されていないファイルがある" }
		}
		return $null
	}

	Write-Output "== worktree"
	$removedBranches = @()
	foreach ($wt in $worktrees | Select-Object -Skip 1) {
		if ($wt.Prunable) { continue }
		if (-not $wt.Branch) {
			Write-Output "残す: $($wt.Path)(detached HEAD)"
			continue
		}
		$reason = Test-Removable $wt.Branch $wt.Path
		if ($reason) {
			Write-Output "残す: $($wt.Path) [$($wt.Branch)]($reason)"
			continue
		}

		Write-Output "削除: $($wt.Path) [$($wt.Branch)](PR #$(Get-MergedPr $wt.Branch) はマージ済み)"
		$done = $false
		if ($hasOrca -and -not $DryRun) {
			$result = orca worktree rm --worktree "path:$($wt.Path)" --json 2>$null | Out-String
			$done = ($LASTEXITCODE -eq 0) -and ($result -match '"ok":\s*true')
			if (-not $done) { Write-Output "  Orcaでは消せなかったため、gitで消します。" }
		}
		if (-not $done) {
			$path = $wt.Path
			if (-not (Invoke-Step "git worktree remove $path" { git worktree remove $path })) {
				Write-Warning "worktreeを削除できませんでした: $path"
				continue
			}
		}
		$removedBranches += $wt.Branch
	}

	Write-Output "== ローカルブランチ"
	$checkedOut = @($worktrees | Where-Object { -not $_.Prunable } | ForEach-Object { $_.Branch }) | Where-Object { $_ -and $_ -notin $removedBranches }
	foreach ($branch in git for-each-ref --format='%(refname:short)' refs/heads) {
		if ($branch -in $checkedOut) { continue }
		# Orcaがworktreeと一緒に消したブランチは、ここではもう存在しない
		git show-ref --verify --quiet "refs/heads/$branch"
		if ($LASTEXITCODE -ne 0) { continue }
		$reason = Test-Removable $branch $null
		if ($reason) {
			if ($reason -ne "保護するブランチ") { Write-Output "残す: $branch($reason)" }
			continue
		}
		Write-Output "削除: $branch(PR #$(Get-MergedPr $branch) はマージ済み)"
		if (-not (Invoke-Step "git branch -D $branch" { git branch -D $branch | Out-Null })) {
			Write-Warning "ブランチを削除できませんでした: $branch"
		}
	}

	Write-Output "== ルートのチェックアウト"
	$rootBranch = git rev-parse --abbrev-ref HEAD
	$rootDirty = git status --porcelain --untracked-files=no
	if ($rootBranch -ne "develop") {
		Write-Output "pullしない: ルートが develop ではない($rootBranch)"
	}
	elseif ($rootDirty) {
		Write-Output "pullしない: 追跡しているファイルに変更がある"
	}
	else {
		if (-not (Invoke-Step "git pull --ff-only" { git pull --quiet --ff-only })) {
			Write-Warning "git pull --ff-only に失敗しました(developに手元だけのコミットがある可能性があります)。"
		}
		elseif (-not $DryRun) {
			Write-Output "develop: $(git log --oneline -1)"
		}
	}
}
finally {
	Pop-Location
}
