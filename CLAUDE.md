# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

DXForgeEX is a from-scratch 3D game engine + WPF level editor, built by following the "Game Engine Series" (TheGameEngineers / Arash Khatami, `Primal` engine: https://github.com/TheGameEngineers/Primal) on YouTube. The codebase intentionally mirrors Primal's architecture: a platform-agnostic C++ engine core, a thin C-ABI DLL boundary, a C#/WPF editor that drives the engine through that DLL, and an offline content-tools pipeline (FBX/texture import) feeding a runtime content/asset system. When picking up unfinished work, treat Primal's commit history/video order as the reference roadmap for "what comes next."

Current implementation reaches roughly the PBR direct-lighting stage (GGX/Smith/Schlick BRDF in `Shaders/BRDF.hlsli`) but has no Image-Based Lighting (cubemap conversion, diffuse/specular prefiltering, BRDF LUT) yet — that is the next logical milestone if asked to continue the renderer.

## Solution layout

`DXForge.sln` has 4 solution configurations, all `x64`: `Debug`, `DebugEditor`, `Release`, `ReleaseEditor`. The `*Editor` configs build the engine as the `DXForgeEngineDLL` dynamic library for use by the editor; the plain `Debug`/`Release` configs build `EngineTest` as a standalone native executable that exercises the engine without the editor.

- **DXForgeEngine** (`StaticLibrary`) — the engine core. Linked into both `EngineTest` and `DXForgeEngineDLL`.
- **DXForgeEngineDLL** (`DynamicLibrary`) — thin C-ABI wrapper (`dllmain.cpp`, `EngineAPI.cpp`, `EntityAPI.cpp`) exposing `DXForgeEngine` to the C# editor via `DllWrappers`.
- **EngineTest** (`Application`) — standalone native test harness (`Main.cpp`, `TestRenderer.cpp`, `Lights.cpp`, `Scripts.cpp`, `RenderItem.cpp`) for driving/visually testing the engine without the editor or a game project. Interactive: requires a window and a GPU, and gates on keyboard input — not suitable for CLI/CI.
- **EngineUnitTests** (`Application`) — headless automated test runner (Debug/Release only, no Editor configs). Links `DXForgeEngine` but needs no window or GPU device; covers pure-logic modules only (currently `Common/Id.h`, `Utilities/FreeList.h`). Uses a dependency-free micro test framework (`MicroTest.h`: `DXFORGE_TEST_CASE`/`CHECK`/`REQUIRE`) rather than a vendored library. Exits non-zero on any failed check; `--json` prints a machine-readable summary. This is the first piece of a longer-term "AI/CLI-friendly engine" direction (see root `README.md`) — favor extending this runner (new `Tests_*.cpp` files, or eventually command-line-driven headless engine operation) over reviving `EngineTest` for anything that needs to run unattended.
- **ContentTools** (`DynamicLibrary`) — offline asset pipeline: FBX import (`FbxImporter.*`, Autodesk FBX SDK), texture import (`TextureImpoter.cpp`, DirectXTex), procedural primitive meshes (`PrimitiveMesh.*`), tangent generation (MikkTSpace), normal-map detection. Consumed by the editor, not linked into the runtime engine.
- **DXForgeEditor** (C#/.NET WPF, `ConfigurationType` mapped to the Editor configs) — project browser/creation, scene & game-entity editing (undo/redo commands), content browser, texture/geometry editors, Visual Studio automation for generating and building per-game-project C++ solutions (`ProjectTemplates/*`, `GameDev/VisualStudio.cs`).

Build via Visual Studio 2022 (`PlatformToolset v143`, open `DXForge.sln`) or via the CLI: root `build.ps1` wraps MSBuild (native) and `dotnet build` (editor) and can run `EngineUnitTests` afterwards — see `README.md` for the parameter list. It summarizes build output by default (see Keeping sessions cheap below). **Always build native projects through `DXForge.sln`** (directly invoking a `.vcxproj` makes `$(SolutionDir)` fall back to the project's own directory, breaking the `AdditionalIncludeDirectories` paths — `build.ps1` already does this correctly via `/t:<ProjectName>`). `GetDXC.ps1` fetches the DirectX Shader Compiler package used for shader compilation (`packages/DirectXShaderCompiler`); it downloads the *latest* DXC release whenever `packages/DirectXShaderCompiler` is empty, so CI caches that directory to avoid GitHub API rate limits. Only `EngineTest` (runtime shader compilation) uses DXC, so `Restore.ps1` fetches it only when `EngineTest` is part of the build (`Debug`/`Release` with no `-Project`, or `-Project EngineTest`); `-Project EngineUnitTests` and the `*Editor` configs build offline without it (issue #61). `EngineUnitTests` covers only pure-logic modules; rendering/GPU code (`Graphics/Direct3D12/*`) still has to be verified by running `EngineTest` or the editor and visually inspecting the render output.

**New C++ source files must be saved with a UTF-8 BOM.** Every existing file in this repo starts with a Japanese header comment (see below); without a BOM, MSVC misdetects the file as the local codepage (Shift-JIS) instead of UTF-8 and corrupts the token stream, producing unrelated-looking cascading compile errors. If a new file produces confusing errors at/after its first Japanese comment, check for a missing BOM first.

## Quality gates

- **CI** (`.github/workflows/ci.yml`): on push to `main`/`develop` and on every PR — native build + `EngineUnitTests` in both `Debug` and `Release` (Release defines `NDEBUG`, which compiles out the `assert`s that `free_list`/`Id` rely on for self-checks, so both configs are verified), plus a `dotnet build` of the editor. `ContentTools` is deliberately out of CI scope: it needs the Autodesk FBX SDK, which runners don't have, and the `Debug`/`Release` solution configs don't build it anyway. The SDK location is the `FbxSdkDir` property in `ContentTools/FbxSdk.props` (defaults to the standard 2020.3.7 install path; override via env var or `/p:FbxSdkDir=`; a missing SDK fails early with a clear error). Note `/p:` global properties are immutable inside the project, so normalize into a *different* property name rather than reassigning `FbxSdkDir`. The old hardcoded `MSVC\14.42.34433\atlmfc` paths were removed: ATL (`atlsafe.h` in `Script.cpp`/`EngineAPI.cpp`) resolves through the default `$(IncludePath)`/`$(LibraryPath)` as long as the VS "C++ ATL" component is installed.
- **Warnings as errors**: the native side is currently at **zero warnings** in both configs — keep it that way. Local builds stay lenient; CI passes `-WarningsAsErrors`. Run `./build.ps1 -Target native -WarningsAsErrors` to check locally before pushing. **`/p:TreatWarningAsError=true` does not work for C++ projects** (a global property never reaches `ClCompile` item metadata, so the compiler still gets `/WX-`); the gate is implemented as an `ItemDefinitionGroup` in the root `Directory.Build.props`, switched on by `/p:DXForgeWarningsAsErrors=true`. Verified empirically: with the switch the compile line carries `/WX` and an injected `C4189` fails the build. The **editor is also at zero warnings** (issue #9: 272 unique nullable warnings fixed with `<Nullable>enable</Nullable>` kept); the same `-WarningsAsErrors` switch passes `-p:TreatWarningsAsErrors=true --no-incremental` to `dotnet build` (full rebuild, because an up-to-date incremental build re-emits no warnings), and CI uses it. Verified: an injected `CS8602` fails the build. When fixing nullable warnings, prefer honest `?` annotations and `Debug.Assert` over `!`, and never turn a swallowed failure into a thrown exception on a path reached from `async void` commands or `OpenProjectView` — nothing catches it there and the editor crashes.
- **`.editorconfig`**: encodes the measured conventions — `.c/.cpp/.h/.hpp` are UTF-8 BOM + tabs (113/113 files), `.cs`/`.xaml` are UTF-8 BOM + 4 spaces (71/71 files). `.hlsl`/`.hlsli` are UTF-8 **without** BOM + 4 spaces (`charset = utf-8`; the 8 shaders with Japanese comments were converted from Shift-JIS in issue #7). Do not give HLSL a BOM to match C++: fxc (`ContentTools/CompileShaders.cmd`, and Visual Studio's HLSL build for SM 5.x) rejects it with `error X3000: Illegal character in shader file`, while DXC accepts both (verified empirically, including through `#include`).
- **Japanese text must never appear in a narrow string literal.** CI runners are not Japanese-locale: their system code page is 1252, and MSVC converts narrow literals to the execution charset, so Japanese characters there produce one `C4566` per character and fail the `/WX` build (this is real: it is locale-dependent string data, not CI noise). Comments are fine — only literals matter. Use the wide API instead (`OutputDebugStringW(L"...")`, as `D3D12Core.cpp` now does); wide literals are UTF-16 and code-page independent. A full scan found exactly one such literal in 113 C++ files, so this is rare — but note the symptom is Debug-only when the code sits inside `#if _DEBUG`.
- **Do not silence a warning that points at a real defect.** The two `C4100` warnings that existed before the gate turned out to mark a genuine bug: `transform::set_orientation` is an empty function body, yet it is reachable from the public API (`entity.set_orientation()` → `entity_script::set_orientation` → the orientation flag branch in `transform::update`), so the value is silently dropped. This has since been resolved (issue #4) by porting Primal Day 086 (commit `db1c19d`): `set_orientation`/`orientation()` and the `orientation` component flag were removed, and each transform now stores a local frame (`right()`/`up()`/`front()`/`local_frame()`) derived from the rotation quaternion, so a direction is changed by setting a rotation instead.
- **Cross-model review with Codex** (issue #56): before a PR is proposed for merge, run `./CodexReview.ps1 -Pr <N> -Post`. It checks out the PR head into a throwaway worktree under `%TEMP%`, runs `codex review` against the tip of the PR's base branch (both fetched from the repo `gh` resolves into script-private `refs/codex-review/<N>-<runId>/*` refs, deleted afterwards, so `origin` layout does not matter), and posts the result as a PR comment (omit `-Post` to only print it). The point is a reviewer that does not share the author's blind spots: on its first run it found a P2 in PR #52 that both the implementing worker and the coordinator's diff read had missed (the editor never calls `script::update()`, so the `begin_play` pending queue grows on every game-DLL reload). In orchestrated work the **coordinator** runs it (never the implementing worker), forwards each finding to that worker, and the worker either fixes it or replies on the PR with why not; re-run the review after fixes. Merging stays a human decision. Pitfalls, both verified: (1) on Windows, Codex's sandbox is disabled by default, and then even a read-only review has every shell command rejected as `blocked by policy` — Codex then reports it could not read the diff instead of reviewing; the script passes `-c windows.sandbox="unelevated"` per call (the global `~/.codex/config.toml` is untouched) and fails loudly if `blocked by policy` still appears. (2) `codex review` rejects `--base` combined with custom instructions, so the diff range is named inside the prompt. Expect 1–2 minutes per PR; it neither builds nor tests, so it complements CI rather than replacing it. Already-merged PRs work too (issue #58): the base tip already contains the PR, so the script instead diffs the merge commit's two parents and has Codex label each finding "still present" or "fixed since" against the current base tip — merge-commit merges only (every PR so far). Several reviews can run in parallel (fetch uses `--no-write-fetch-head` because the shared `FETCH_HEAD` otherwise races).

## Keeping sessions cheap

Build-tool output is the dominant token cost in this repo. Measured on a full compile: the native
MSBuild log is ~7,400 characters at default verbosity, and `dotnet build` of the editor is
was **~132,000 characters** while it had 588 nullable warnings (272 unique, printed twice because of the
WPF temp project; fixed in issue #9 — the editor's raw minimal-verbosity log is now ~470 characters).
`build.ps1` still summarizes by default — errors verbatim, warnings folded into per-code counts — so a
regression cannot flood the session again.

- **Never pass `-DetailedLog` unless actually diagnosing a build failure**, and never re-run a build
  just to see its full log when the summary already named the error.
- **Scope the build**: `-Target native -Project EngineUnitTests` for the test loop. Building the
  whole solution, or `-Target all`, costs much more and is rarely needed.
- **Reading CI logs**: `gh run view <id> --json conclusion,jobs` first. `--log-failed` dumps the raw
  log (the C4566 failure was 43 near-identical lines); only reach for it when the job summary is not
  enough, and pipe it through `tail`/`grep`.
- Don't re-read a file right after writing it, and prefer `sed -n '<range>p'` over reading whole
  files when a specific function is what matters.
- **Write edit scripts to a file, don't inline them.** A quoted heredoc passed to `python -`/`pwsh -Command`
  loses backslashes on the way in: Windows paths break (`swhere` becomes a vertical tab), `
` becomes a
  real newline so patterns stop matching, and regex character classes like `[^"\]` fail to compile. Write the
  script (or the replacement text) into the scratchpad directory with a quoted heredoc and run it by path, or
  match on a backslash-free substring. Each mangled attempt is a wasted round trip.
- `python3` on this machine is the Windows Store stub and silently does nothing — use `python`.
- Verify with the fast path: `EngineUnitTests` is seconds; a full `Debug`+`Release` sweep is minutes
  and only needed before pushing.

# Compact instructions

When compacting, keep: the pitfalls in this file, which issues are open and their decisions, exact
build/test commands used, and any measured numbers. Drop: full build logs, file contents already
written to disk, and superseded intermediate attempts.

## Engine architecture

- **Platform abstraction**: `Platform/Platform.h` + `PlatformWin32.cpp` wrap OS window handling behind a platform-independent interface; `Window.h` is the cross-platform window handle type.
- **Graphics abstraction**: `Graphics/GraphicsPlatformInterface.h` defines a `platform_interface` struct of function pointers (surface, light, camera, submesh/texture/material, render) that each graphics backend fills in. `Graphics/Renderer.cpp` selects a backend (`graphics_platform::direct3d12/vulkan/opengl`) and forwards calls to it. **Only Direct3D12 (`Graphics/Direct3D12/`) is actually implemented**; the Vulkan/OpenGL folders exist as stubs for future backends — do not assume they work.
- **D3D12 backend** (`Graphics/Direct3D12/`): `D3D12Core` (device/queues/sync), `D3D12Surface` (swap chain per window), `D3D12Resource`/`D3D12Helpers` (descriptor heaps, deferred resource release, free-list-backed id allocation), `D3D12Upload` (copy-queue GPU uploader), `D3D12Content` (GPU-side submesh/texture/material storage), `D3D12GPass` (depth prepass + geometry/lighting pass, draw-indirect), `D3D12Light`/`D3D12LightCulling` (tiled light culling via compute shader — frustum grid build + culling, see `Shaders/GridFrustums.hlsl`/`CullLights.hlsl`), `D3D12PostProcess` (fullscreen post pass). Engine HLSL shaders live under `Graphics/Direct3D12/Shaders/` and are precompiled offline into a `shaders.bin` blob referenced by `Renderer.cpp`'s `engine_shader_paths`.
- **ECS**: `Components/Entity.*` is the game-entity container; `Transform`, `Script`, `Geometry` are components attached via generational IDs (`Common/Id.h`). `EngineAPI/*.h` are the public structs/handles (camera, light, game entity, transform/script/geometry component, input) exposed across the DLL boundary — these mirror internal types but are kept ABI-stable for the editor and game code.
- **Content pipeline (runtime side)**: `Content/ContentToEngine.*` converts compiled asset blobs (meshes, textures, materials, shaders — see `asset_type`/`texture_flags` enums) into GPU resources via the active graphics backend; `Content/ContentLoader.*` handles loading the game DLL and engine shader blob (editor/dev builds only, guarded by `!defined(SHIPPING)`).
- **Content pipeline (offline side, `ContentTools`)**: separate from the runtime; produces the asset files `ContentToEngine` consumes. FBX scenes are traversed/packed into submesh+LOD hierarchies; textures go through DirectXTex import/compression; tangents are generated with MikkTSpace for normal mapping.
- **Game code**: each game project gets its own generated Visual Studio solution/project (from `DXForgeEditor/ProjectTemplates/*`), built via editor-driven VS automation (`GameDev/VisualStudio.cs`) and loaded as a DLL at runtime (`ContentLoader::load_game`).

## Editor architecture (DXForgeEditor, C#/WPF)

- `GameProject/` — project creation/open (`NewProject.cs`, `OpenProject.cs`), the `Project`/`Scene` models, and the undo/redo command system backing entity/component edits.
- `Components/` — editor-side mirrors of engine components (transform, script, geometry) with multi-selection support, bound to WPF views.
- `DllWrappers/` — P/Invoke boundary calling into `DXForgeEngineDLL`.
- `Content/ContentBrowser/`, `Editors/TextureEditor/`, `Editors/GeometryEditor/`, `Editors/WorldEditor/` — asset browsing and per-asset-type editors (texture detail/RGBA channel view, geometry/primitive preview, scene/world viewport with its own render surface).
- `Utilities/RenderSurface/` — hosts a Win32 D3D12 render surface inside WPF (HwndHost-style interop) for viewport rendering.

## Conventions to follow when adding/editing C++ files

Existing engine/editor C++ source files begin with a standardized Japanese header comment block giving the filename, creation date, author, a one-line summary, and an update history log (date + change description per entry), e.g.:

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

When modifying an existing file this way, append a new dated line to the 更新履歴 (update history) rather than rewriting prior entries. New files should follow the same header format for consistency, append-only history. All engine code lives under the `dxforge` namespace (sub-namespaced per module, e.g. `dxforge::graphics`, `dxforge::content`).
