---
name: gui-check
description: Run the DXForgeEX editor or game in the GUI to verify behavior, and put the results and screenshots in the PR body. Use when executing the GUI check steps written in a PR, or when the user says 「GUI 確認を行って」「エディタを起動して確認して」. The coordinator does not operate the GUI itself; it hands the check to a separate worker with start-gui-check.ps1.
---

# Checking in the GUI

<!-- last-reviewed: 2026-11-07 -->

Editor/game behavior and rendering results cannot be checked by automated tests, so operate the actual screen.

## The coordinator uses `start-gui-check.ps1`

```powershell
./.claude/skills/gui-check/start-gui-check.ps1 -Pr <N>           # start (-DryRun shows the spec, -Size normal for the larger model)
./.claude/skills/gui-check/start-gui-check.ps1 -Pr <N> -Cleanup  # delete the worktree after worker_done and worker-release
```

It reads the PR body's `## GUI の確認手順` section (numbered) and starts a new worker with a spec made from it plus `worker-spec.md`. The worker builds the PR head, checks it, puts the images in the PR body and returns only one line of result per step in `worker_done` (subject `GUI 確認 PR #N: PASS|NG|未完了`). Before merging, confirm that the `head:` in `worker_done` equals the current PR head; if not (something was pushed after the check), check again. The coordinator does not operate the GUI or read the images (a hook blocks it). Mouse and keyboard input affects the whole desktop, so run only one GUI check at a time (if a `gui-check-pr-*` worktree is left or another start is in progress, it refuses with exit code 4).

## The worker uses `gui.ps1`

Elements are re-read through UI Automation on every action, so indices never go stale, and child windows (dialogs) are readable too. Do not use `orca computer`. Detailed parameters are at the top of the script.

```powershell
$gui = "./.claude/skills/gui-check/gui.ps1"
$out = & $gui -Action create -Project GuiCheck200                # last line is PID <n>
$p = [int](($out | Select-Object -Last 1) -replace 'PID ', '')
& $gui -Action close -ProcId $p

# Fill the project without the GUI (close the editor first; while it is open dxforge refuses with 44)
& $gui -Action prepare -Project GuiCheck200 -Import F:\DXForgeTestAssets\LODTest.fbx
& $gui -Action prepare -Project GuiCheck200 -Op '{"op":"create-entity","name":"Box"}'   # -Ops <file> for several

$out = & $gui -Action open -Project GuiCheck200                  # baseline build: -Exe <exe>
$p = [int](($out | Select-Object -Last 1) -replace 'PID ', '')
& $gui -Action tree -ProcId $p -Pattern 'ListItem|^Text'         # judge by text first
& $gui -Action click -ProcId $p -Target AddEntity                # list of names: -Action names
& $gui -Action click -ProcId $p -Target 'el:Text:^Box$'          # by element; Text is clicked at its left edge
& $gui -Action drag -ProcId $p -Target 'el:Text:^lodtest$' -To EntityDrop
& $gui -Action keys -ProcId $p -Keys '{F5}'
& $gui -Action shot -ProcId $p -Region Hierarchy -OutFile $env:TEMP\gui-check-pr-200\pr-hierarchy.png
& $gui -Action switch -ProcId $p -Project GuiCheck200b           # File > Open Project; to create: -Action create -ProcId $p
& $gui -Action close -ProcId $p                                  # also closes a leftover Project Browser and confirms the exit
& $gui -Action end-session -ProcId $p                            # simulates logoff for that process only and checks the exit code (no real logoff)
& $gui -Action remove-project -Project GuiCheck200               # only names starting with GuiCheck
& $gui -Action diff -Dir $env:TEMP\gui-check-pr-200              # pixel difference of base-*.png vs. pr-*.png
```

Already handled by the script: tabs and lists are clicked with the real mouse. ALT-based activation only happens when the target is not in front, so open menus stay open. Hierarchy rows are clicked at the left edge, so Remove is not hit by mistake. Drop onto an empty list via `EntityDrop`.

- Prefer text and numbers for judging. Capture narrow regions (`-Region` name or `x,y,w,h`, or a `-Target` element; width is capped at 1000 by default). Coordinates are physical, from the window's top-left; named coordinates are for the default window size and are scaled to the display scale.
- Address dialogs with `-Title <part of the title>`. `window.ps1 -Action list -ProcId <PID>` lists them.
- Capture debug output (`[DXForge] ...`, the D3D12 debug layer) with `capture-debug-output.ps1` (usage at the top of the script).

## Preparation

- Build the PR worktree: `./build.ps1 -Configuration DebugEditor -Target all` and `./build.ps1 -Configuration Debug -Target native -Project DXForgeEngine` (games link against Debug).
- The editor builds games with the engine of the repository its own executable is in (#117). Do not switch the user environment variable `DXFORGE_ENGINE`.
- Create a fresh check project named `GuiCheck<N>`; never use existing ones. Location: user environment variable `DXFORGE_PROJECTS`, else `%USERPROFILE%\DXForgeProjects\` (#141).
- Without a source file, neither the game exe nor the DLL is produced (#118). Add a script with New Script. To tell whether a script ran, have `begin_play`/`update` write a marker file.
- EngineTest reads `..\..\x64\*.model` and `*.texture` (not in git), so start it with `x64\Debug` as the working directory. In a new worktree, copy them from the root checkout's `x64` and delete them afterwards.

## Recording the result

**Whenever you checked on screen, always put screenshots in the PR body**, not only in a comment. A check without a PR goes in a comment on the related issue.

- Put before/after pairs `base-<name>.png` and `pr-<name>.png` in one folder (`%TEMP%\gui-check-pr-<N>\`). The baseline is develop (the root checkout's executable is fine; do not switch its branch).
- Publish with `./.claude/skills/gui-check/push-pr-assets.ps1 -Pr <N> -Dir <folder> -Summary <result Markdown> -BaseLabel "develop(<sha>)"`. It pushes to `pr-<N>/gui-check/` on the `pr-assets` branch and replaces the `<!-- gui-check:start -->` section of the body (one section even after reruns). `-DryRun` previews it.
- `-Summary` (Japanese) contains the check date, the compared versions (sha) and the result per step (as expected or not, numbers).

## Cleanup

Close every game, VS and editor you started (the worktree cannot be deleted while its DLLs are held). Make sure no process is left stuck on an assert dialog. Delete check projects with `-Action remove-project`.
