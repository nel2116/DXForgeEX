# Review, merge and cleanup

How a PR gets from "done" to merged into `develop`, and how local worktrees are cleaned up afterwards. The rules to follow are summarized in the root `AGENTS.md`; this file has the mechanics and the verified pitfalls.

## Cross-model review with Codex (`CodexReview.ps1`, issue #56)

Before a PR is proposed for merge, run `./CodexReview.ps1 -Pr <N> -Post`. It checks out the PR head into a throwaway worktree under `%TEMP%`, runs `codex review` against the tip of the PR's base branch (both fetched from the repo `gh` resolves into script-private `refs/codex-review/<N>-<runId>/*` refs, deleted afterwards, so `origin` layout does not matter), and posts the result as a PR comment (omit `-Post` to only print it). Codex reads the root `AGENTS.md` of the checked-out PR head, so the project rules there apply to its review.

The point is a reviewer that does not share the author's blind spots: on its first run it found a P2 in PR #52 that both the implementing worker and the coordinator's diff read had missed (the editor never calls `script::update()`, so the `begin_play` pending queue grows on every game-DLL reload).

In orchestrated work the **coordinator** runs it (never the implementing worker), forwards each finding to that worker, and the worker either fixes it or replies on the PR with why not; re-run the review after fixes.

Verified pitfalls:

1. On Windows, Codex's sandbox is disabled by default, and then even a read-only review has every shell command rejected as `blocked by policy` — Codex then reports it could not read the diff instead of reviewing. The script passes `-c windows.sandbox="unelevated"` per call (the global `~/.codex/config.toml` is untouched) and fails loudly if `blocked by policy` still appears.
2. `codex review` rejects `--base` combined with custom instructions, so the diff range is named inside the prompt.

Expect 1–2 minutes per PR; it neither builds nor tests, so it complements CI rather than replacing it. Already-merged PRs work too (issue #58): the base tip already contains the PR, so the script instead diffs the merge commit's two parents and has Codex label each finding "still present" or "fixed since" against the current base tip — merge-commit merges only (every PR so far). Several reviews can run in parallel (fetch uses `--no-write-fetch-head` because the shared `FETCH_HEAD` otherwise races).

## Auto-merge into `develop`

Branch protection on `develop` requires five checks — the three CI jobs, `No hold label` (`.github/workflows/pr-automation.yml`), and the commit status `codex-review` — with admins included, so nothing (not even an admin `gh pr merge`) lands without them.

- The coordinator runs `./CodexReview.ps1 -Pr <N> -Post -SetStatus` **only once the PR's completion criteria are met** (CI green, GUI checks done, earlier findings resolved).
- The verdict is read from `codex review`'s own output format — every finding is rendered as a `- [P0..P3] title — file:lines` line under `Full review comments:` — because `codex review` ignores custom instructions about output format (a requested `VERDICT:` line never appeared; found by Codex itself on PR #96). Any finding, regardless of severity → `codex-review` = failure.
- Success (on the reviewed SHA, followed by `gh pr merge --auto --merge`) needs positive evidence too: no finding line **and** the phrase 「問題は見つかりませんでした」, which the prompt asks for and which free-text summaries do honor; anything else (e.g. `Reviewer failed to output a response.`) → failure, so an incomplete review never passes.
- If a Codex upgrade changes that rendering, a review with findings would read as PASS, so re-check the format after upgrading `codex-cli` (verified on 0.163.0).
- `-SetStatus` refuses (before reviewing) unless the base branch's protection requires both `codex-review` and `No hold label`, and re-reads the PR's base before scheduling the merge, since a base changed mid-review would bypass the protection check.
- A status belongs to a commit, so pushing a fix drops it and the PR waits for a fresh review — no unreviewed fix can merge.
- A human stops a merge by adding the `hold` label (also works after `codex-review` passed); removing it re-runs the gate.
- GitHub deletes the head branch on merge (`delete_branch_on_merge`), and the `close-issues` job closes `Closes #NN` issues, which GitHub itself only does for PRs into the default branch (`main`). It runs on `push` to `develop` and looks up the merged PR from the pushed commits (`commits/<sha>/pulls`), because both obvious triggers fail: a fork PR's `pull_request` token is read-only, and `pull_request_target` always runs the *default branch's* (`main`'s) copy of the workflow, so a workflow that exists only on `develop` never fires (both found by Codex on PR #96).
- Pitfall: `gh pr merge --auto` merges *immediately* when no required check is pending, so never use it on a branch without this protection.

## Local cleanup (`CleanupMerged.ps1`)

`./CleanupMerged.ps1` (`-DryRun` to preview) removes a worktree/local branch only if its PR is merged, its tip is contained in `origin/develop`, and the worktree has no changes or untracked files (so a freshly created, still-empty worker worktree is never touched), then fast-forwards the root `develop`. Run it without being asked after merges and in the pre-flight check of an orchestration.

- `orca worktree rm` can return success before Orca has finished deleting the branch/folder (issue #104: the following `git branch -D` failed with `used by worktree`), so after it the script polls every 0.5 s for up to 15 s until the worktree registration, `refs/heads/<branch>` and the folder are all gone; a branch still left is deleted with `git branch -D`, a registration still left gets a warning and its branch is not touched. The local-branch pass re-reads `git worktree list` instead of trusting what the worktree pass removed.
- If the folder survives (a process holds a DLL from it — on 10/27 a pwsh stuck in a Debug assert dialog), it warns and lists processes whose `Get-Process` `Modules` lie under the folder (name + PID); it never kills them or deletes the folder, and cannot see 32-bit processes' DLLs, non-DLL file handles, or processes it lacks rights to. `-DryRun` neither waits nor scans.
- Test: `pwsh -NoProfile -File Tests/CleanupMerged.Tests.ps1` (~1 min; disposable repos under `%TEMP%`, fake `gh.ps1`/`orca.ps1` put first on `PATH` and verified via `Get-Command` before each run, so the real GitHub/Orca are never called; `-ScriptPath` runs it against another copy, e.g. the pre-fix script, `-Case` picks cases); CI runs it in the `Editor build` job.
