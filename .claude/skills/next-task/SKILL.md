---
name: next-task
description: Propose what to work on next in DXForgeEX from the repository's state (develop CI, open PRs and issues, Primal progress, pending checks). Use when the user asks 「次に行うべきタスクは?」「他にやるべきことは?」「次は何をする?」. It only proposes; it does not start the work.
---

# Proposing the next task

<!-- last-reviewed: 2026-10-29 -->

After this question the user usually follows with 「そのためのプロンプトを提示して」, so **only propose; do not start the work**. The instructions for the chosen task are written with the `orchestration-prompt` skill.

## 1. Gather the state

```bash
git fetch -q origin
gh run list --branch develop --workflow ci.yml --limit 1 --json conclusion,headSha,displayTitle
gh pr list --state open --json number,title,labels,headRefName
gh issue list --state open --limit 50 --json number,title,labels
git worktree list
python Tests/check_conventions.py | grep '^warning:'   # docs not reviewed for over 90 days
```

- For each open PR, find out from its body and comments why it is still open (waiting for a GUI check, `hold` label, pending Codex findings, ...).
- Collect pending checks from memory and from PR bodies' 「マージ後に確かめる項目」 and 「GUI の確認手順」.
- Judge Primal progress from `Primal Day NNN` in issue titles and the "Renderer status" in `docs/dev/Architecture.md`. If the next Day has no issue yet, that is a candidate too (order of Primal's commit history: https://github.com/TheGameEngineers/Primal).

## 2. Prioritize

Higher items first:

1. **develop's CI is failing** -> fix before anything else.
2. **Nearly finished work**: PRs with only a GUI check left, or Codex findings left to handle. Close these before starting new work.
3. **Bugs that stall development**: build, test, review, merge or cleanup failures. They affect all future work.
4. **The next Primal Day** (feature progress).
5. **Making the engine easier to drive from AI/CLI** (continuing #11, widening what can be verified unattended).
6. Other improvements. Reviewing stale docs (the `warning:` lines above) also goes here: one small PR per doc, procedure in the `writing-docs` skill.

Also note for each:

- **GUI check needed?** If the user is away, prefer work that can be verified headless to completion.
- **Parallelizable?** Work that edits disjoint files can go to separate workers in parallel.
- **Size**: does it fit one PR? If not, show how to split it.

## 3. Present

Briefly, in Japanese:

- State summary (2-3 lines): develop CI, open PRs, pending checks.
- Candidate table (3-5 rows): task (issue number), reason, size, GUI check needed, parallel partner.
- **Recommendation**: one (or a parallel combination) and why.
- End by asking 「この作業のプロンプトを作りますか?」 and stop.
