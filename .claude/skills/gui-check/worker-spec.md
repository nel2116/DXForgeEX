<!-- last-reviewed: 2026-11-05 -->

## Task: GUI check of PR #{{PR}} (#195)

PR: {{URL}} ("{{TITLE}}"). Head to check: `{{HEAD}}` (branch `{{HEAD_REF}}`, base `{{BASE_REF}}`).

You are a worker that only does GUI checks, separate from the implementer. Check what the steps say with eyes that do not know the implementer's intent.

### Differences from the common rules (these win)

- Use the GUI, through `.claude/skills/gui-check/gui.ps1` (usage: `SKILL.md` of the `gui-check` skill). Do not use `orca computer`.
- First switch to the PR head: `git fetch origin refs/pull/{{PR}}/head`, check that `git rev-parse FETCH_HEAD` equals the head above, then `git checkout --detach {{HEAD}}`. If it differs (the PR was updated), report 未完了 without checking.
- Do not rename the branch. Do not commit, push (except through `push-pr-assets.ps1`), create a PR, run `PrePr.ps1`, wait for CI or run `CodexReview.ps1`. Do not fix product code.
- Do not wait for permission with `ask`. When done, send `worker_done` once in the format below and finish.
- Use `ask` only when the steps cannot be followed or you cannot judge the result. If you find a bug, do not fix it or file an issue; report it as NG.

### How to proceed

1. Build: `./build.ps1 -Configuration DebugEditor -Target all`, then `./build.ps1 -Configuration Debug -Target native -Project DXForgeEngine`.
2. Create the check project named `GuiCheck{{PR}}` (then `GuiCheck{{PR}}b` and so on). Fill it with `gui.ps1 -Action prepare` where possible. Do not use existing projects.
3. Check the steps below in order. Judge first by `-Action tree` text, logs, files and numbers. `Read` an image only when the look itself is what is being judged, and capture a narrow region. For steps that need the screen before the change, start `x64\DebugEditor\DXForgeEditor.exe` of the root checkout (`F:\HAL\Self\DXForgeEX`; do not switch its branch) with `-Exe`, and make `base-<name>.png` / `pr-<name>.png` pairs.
4. Put images in `%TEMP%\gui-check-pr-{{PR}}\`, write the result Markdown (in Japanese: date, checked head, result per step), and put it in the PR body with `./.claude/skills/gui-check/push-pr-assets.ps1 -Pr {{PR}} -Dir <folder> -Summary <Markdown>`.
5. Clean up: close every editor, game and VS you started (`-Action close`), delete the check project with `-Action remove-project`, delete the `%TEMP%` folder.

### Report format

The `worker_done` `--subject` is one of `GUI 確認 PR #{{PR}}: PASS` (all as expected), `NG` (some step differs from the expectation) or `未完了` (some step could not be checked). `--outcome` is `succeeded` once all steps were checked, even with NG, and `failed` otherwise. `--body` is exactly the form below and nothing else (no image descriptions or full logs, to keep the coordinator's context small). It replaces "what to write in worker_done" from the common rules.

```text
head: <7 chars>
1. OK: <what you saw or measured, one line>
2. NG: <expected vs. actual, one line>
3. 未確認: <reason, one line>
本文: <PR URL> (画像 <n> 枚)
片付け: エディタ終了、<deleted projects>
```

### Steps (taken from the PR body)

{{STEPS}}
