# AGENTS.md

Shared instructions for coding agents (Claude Code reads this through `CLAUDE.md`; Codex reads it directly, including in `CodexReview.ps1` reviews). Keep it short: it is a map, and the details live in `docs/dev/`. Add a rule here only after a mistake has happened twice, and prefer a check in `Tests/check_conventions.py` or a hook over a sentence when the rule has no exceptions.

## Project

DXForgeEX is a from-scratch 3D game engine + WPF level editor that follows the "Game Engine Series" (`Primal` engine: https://github.com/TheGameEngineers/Primal). It mirrors Primal's architecture: a C++ engine core (`DXForgeEngine`), a thin C-ABI DLL (`DXForgeEngineDLL`), a C#/WPF editor (`DXForgeEditor`) that drives the engine through it, and an offline asset pipeline (`ContentTools`, FBX/texture import). For "what comes next", follow Primal's commit history/video order. Only the Direct3D12 backend exists; the Vulkan/OpenGL folders are stubs.

## Where to read more

| When you touch... | Read |
|---|---|
| engine/editor structure, renderer status, which project does what | `docs/dev/Architecture.md` |
| build scripts, DXC, FBX SDK, warnings, encodings, tests, CI | `docs/dev/BuildAndTest.md` |
| `DXForgeImport/` | `docs/dev/DXForgeImport.md` |
| PR review, merging, `CodexReview.ps1`, `CleanupMerged.ps1` | `docs/dev/ReviewAndMerge.md` |
| editor behavior that needs a manual GUI check | `docs/ManualVerification.md` |
| `build.ps1` parameters, human-facing overview | `README.md` |

## Commands

```powershell
./build.ps1 -Target native -Project EngineUnitTests -RunTests      # fast loop (seconds)
./build.ps1 -Target native -Configuration Release -RunTests -WarningsAsErrors
./build.ps1 -Target editor -WarningsAsErrors                       # editor + DXForgeImport
python Tests/check_conventions.py                                  # BOM / literals / headers
```

- Iterate with the fast loop; a full `Debug`+`Release` sweep takes minutes and is only needed before pushing.
- Always build native projects through `DXForge.sln` (`build.ps1` does); building a `.vcxproj` directly breaks `$(SolutionDir)`.
- Use `python`, never `python3` (on this machine it is the Windows Store stub and silently does nothing).
- `EngineTest` needs a window and a GPU and waits for keyboard input; never run it unattended. Extend `EngineUnitTests` (new `Tests_*.cpp`, or its headless `--game-bin` mode) for anything that must run without a human.

## Rules that break the build

These are checked by `Tests/check_conventions.py` (CI, and a Claude Code hook after every edit).

- C/C++, C#, XAML and `.ps1` files are UTF-8 **with BOM**. Engine HLSL with non-ASCII text needs the BOM too; HLSL under `ContentTools/` and `DXForgeEditor/` (compiled by fxc) must have **no** BOM. Fix with `python Tests/check_conventions.py --fix <file>`.
- No Japanese (non-ASCII) text in narrow string or char literals in C/C++ — CI's code page 1252 turns it into `C4566` → `/WX` failure. Use `L"..."` and the wide API (`OutputDebugStringW`). Comments are fine.
- Native and editor builds are at zero warnings, and CI treats warnings as errors. Do not silence a warning without checking whether it points at a real defect (it has before: issue #4).

## Code conventions

- C++: tabs; everything under the `dxforge` namespace, sub-namespaced per module (`dxforge::graphics`, `dxforge::content`).
- Every engine/editor C++ file starts with this header; when you modify a file, append a dated line to 更新履歴 (never rewrite earlier lines). New files use the same header.

```cpp
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [FileName.cpp]
// 作成日 : YYYY/MM/DD
// 作成者 : 田中ミノル
// 概要
// 　一行概要
// 更新履歴
// YYYY/MM/DD 変更内容
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
```

- C# nullable warnings: prefer honest `?` annotations and `Debug.Assert` over `!`. Never turn a swallowed failure into a thrown exception on a path reached from `async void` commands or `OpenProjectView` — nothing catches it and the editor crashes.
- Commit messages, PRs, issues and code comments are in Japanese, following the existing history (commit subjects start with `[add]`, `[fix]` or `[chore]`).

## Definition of done

- The change builds with `-WarningsAsErrors` in the configs it affects, and `EngineUnitTests` plus any test next to the touched code pass. Report the command and its result, not "tests pass".
- New behavior that can run headless has a test. Rendering/GPU and editor GUI changes have no automated test: check them by running the app (see `docs/ManualVerification.md`) and say what you saw. Whenever the app was checked on screen, put before/after screenshots in the PR body (`gui-check` skill, `push-pr-assets.ps1`), not only in a comment.
- PRs target `develop`. Merging is gated by CI plus a Codex review; see `docs/dev/ReviewAndMerge.md`. Never use `gh pr merge --auto` on a branch without that protection — it merges immediately.

## Keeping sessions cheap

Build output is the dominant token cost here. `build.ps1` summarizes it by default; pass `-DetailedLog` only when diagnosing a build failure, and never re-run a build just to see a log the summary already explained. Scope builds (`-Project EngineUnitTests`) rather than building everything. For CI, read `gh run view <id> --json conclusion,jobs` first and only then `--log-failed`, filtered through `tail`/`grep`.
