# Build and test details

Reference for agents and contributors. The commands to run are in the root `AGENTS.md`; this file explains why they are the way they are. The `build.ps1` parameter list is in `README.md`.

## Building

- Visual Studio 2022 (`PlatformToolset v143`, open `DXForge.sln`) or the CLI: root `build.ps1` wraps MSBuild (native) and `dotnet build` (editor) and can run the tests afterwards. It summarizes build output by default — errors verbatim, warnings folded into per-code counts.
- **Always build native projects through `DXForge.sln`**: directly invoking a `.vcxproj` makes `$(SolutionDir)` fall back to the project's own directory, breaking the `AdditionalIncludeDirectories` paths. `build.ps1` does this correctly via `/t:<ProjectName>`.
- **DXC (two copies, on purpose).**
  - `GetDXC.ps1` fetches the DirectX Shader Compiler package (`packages/DirectXShaderCompiler`); it downloads the *latest* DXC release whenever that folder is empty, so CI caches it to avoid GitHub API rate limits. Only `EngineTest` (runtime shader compilation) uses it, so `Restore.ps1` fetches it only when `EngineTest` is part of the build (`Debug`/`Release` with no `-Project`, or `-Project EngineTest`); `-Project EngineUnitTests` and the `*Editor` configs build offline without it (issue #61).
  - The editor-side shader compilation (`DXForgeEngineDLL`'s `CompileShader`, Primal Day 076, issue #29) uses a separate, version-pinned NuGet package `Microsoft.Direct3D.DXC` (`DXForgeEngineDLL/packages.config`, restored with the other `*Editor` NuGet packages); its targets copy `dxcompiler.dll`/`dxil.dll` next to `DXForgeEngineDLL.dll` (= the editor's output folder).
  - `ShaderCompilation.cpp` (shared with `EngineTest`) loads DXC at run time with `LoadLibraryExW` from its own module's folder instead of linking `dxcompiler.lib`, so a missing DXC only fails the compilation (logged) instead of making `DXForgeEngineDLL.dll` unloadable.
- **FBX SDK**: `ContentTools` needs it, so it is out of CI scope (and the `Debug`/`Release` configs don't build it anyway). The location is the `FbxSdkDir` property in `ContentTools/FbxSdk.props` (defaults to the standard 2020.3.7 install path; override via env var or `/p:FbxSdkDir=`; a missing SDK fails early with a clear error). `/p:` global properties are immutable inside the project, so normalize into a *different* property name rather than reassigning `FbxSdkDir`.
- **ATL** (`atlsafe.h` in `Script.cpp`/`EngineAPI.cpp`) resolves through the default `$(IncludePath)`/`$(LibraryPath)` as long as the VS "C++ ATL" component is installed (the old hardcoded `MSVC\14.42.34433\atlmfc` paths were removed).

## Warnings as errors

- Native: zero warnings in both configs. Local builds stay lenient; CI passes `-WarningsAsErrors`. **`/p:TreatWarningAsError=true` does not work for C++ projects** (a global property never reaches `ClCompile` item metadata, so the compiler still gets `/WX-`); the gate is an `ItemDefinitionGroup` in the root `Directory.Build.props`, switched on by `/p:DXForgeWarningsAsErrors=true`. Verified: with the switch the compile line carries `/WX` and an injected `C4189` fails the build.
- Editor: also zero warnings (issue #9: 272 unique nullable warnings fixed with `<Nullable>enable</Nullable>` kept). The same switch passes `-p:TreatWarningsAsErrors=true --no-incremental` to `dotnet build` (full rebuild, because an up-to-date incremental build re-emits no warnings). Verified: an injected `CS8602` fails the build.
- History of "do not silence a real defect": the two `C4100` warnings that existed before the gate marked a genuine bug — `transform::set_orientation` was an empty body reachable from the public API, so the value was silently dropped (fixed in issue #4 by porting Primal Day 086, commit `db1c19d`).

## Encodings (`.editorconfig`, enforced by `Tests/check_conventions.py`)

- `.c/.cpp/.h/.hpp` are UTF-8 BOM + tabs, `.cs`/`.xaml`/`.ps1` are UTF-8 BOM. Without a BOM, MSVC misdetects a file as the local codepage (Shift-JIS) and corrupts the token stream, producing unrelated-looking cascading compile errors at/after the first Japanese comment.
- `.hlsl`/`.hlsli` are UTF-8 BOM + 4 spaces (the 8 engine shaders with Japanese comments were converted from Shift-JIS in issue #7), **except** the fxc-compiled ones under `ContentTools/` and `DXForgeEditor/`, which must stay BOM-less. Both halves were verified: the engine shaders need the BOM because `Shaders/SharedTypes.h` `#include`s `CommonTypes.hlsli` into the C++ build (BOM-less UTF-8 there gives `C4819` → `/WX` failure), and DXC accepts either; fxc (`ContentTools/CompileShaders.cmd`, the WPF pixel shader, Visual Studio's HLSL build for SM 5.x) rejects a BOM with `error X3000: Illegal character in shader file`, directly or through `#include`. ASCII-only engine shaders without a BOM are harmless and are not flagged.
- **Japanese in a narrow string literal** fails CI: runners use code page 1252, MSVC converts narrow literals to the execution charset, and each Japanese character gives `C4566` → `/WX` failure (real locale-dependent data, not CI noise). Comments are fine. Use the wide API (`OutputDebugStringW(L"...")`, as `D3D12Core.cpp` does). The symptom is Debug-only when the code sits inside `#if _DEBUG`.

## Tests

- **EngineUnitTests** (Debug/Release only, no Editor configs): links `DXForgeEngine` but needs no window or GPU device; covers pure-logic modules only (currently `Common/Id.h`, `Utilities/FreeList.h`). Uses a dependency-free micro test framework (`MicroTest.h`: `DXFORGE_TEST_CASE`/`CHECK`/`REQUIRE`). Exits non-zero on any failed check; `--json` prints a machine-readable summary. Release defines `NDEBUG`, which compiles out the `assert`s that `free_list`/`Id` rely on, so CI runs both configs.
- **Headless mode** (issue #11, first step): `EngineUnitTests --game-bin <path> --frames <N> [--dt s] [--json]` loads a `game.bin` through the same `content::load_game(path)` the game exe uses, calls `script::update(dt)` N times, and reports the last frame's entity transforms as JSON on stdout (logs on stderr). No window, no GPU, no Renderer; the loop/argument parsing lives in `DXForgeEngine/Core/Headless.{h,cpp}` (non-`SHIPPING` only), and scripts must be statically registered in the runner exactly like in a game exe (currently only `EngineUnitTests/HeadlessTestScripts.cpp`). Exit codes: 0 ok (a 0-entity `game.bin` is a valid empty scene, deliberately unlike Primal — #107), 2 usage, 3 abort, 10 not found, 11 unreadable/empty, 12 invalid format, 13 unknown component, 14 unregistered script, 15 entity creation failed (`--help` lists them). `python EngineUnitTests/Tests/headless_run_test.py [--config Release]` checks it as a separate process; `build.ps1 -RunTests` (so CI) runs it too. Not yet there: `.dxproj` loading, script-driven operations, rendering/screenshots, hooking up the #10 ContentTools CLI. This is the start of the "AI/CLI-friendly engine" direction (see `README.md`) — extend this runner rather than reviving `EngineTest` for anything that must run unattended.
- **Shader compilation**: `python DXForgeEngineDLL/Tests/shader_compilation_test.py` (C ABI via ctypes) and `pwsh -STA -File DXForgeEngineDLL/Tests/editor_shader_compilation_test.ps1` (the editor's own `DllWrappers` via reflection, including the Logger output).
- **Editor engine path**: `pwsh -NoProfile -File DXForgeEditor/Tests/engine_path_test.ps1`.
- **Scripts**: `pwsh -NoProfile -File Tests/CleanupMerged.Tests.ps1`, `pwsh -NoProfile -File Tests/Restore.Tests.ps1`.
- **File conventions**: `python Tests/check_conventions.py` (whole repo) and its own test `python Tests/check_conventions_test.py`.
- Rendering/GPU code (`Graphics/Direct3D12/*`) is not covered by any automated test; verify it by running `EngineTest` or the editor and inspecting the output (`docs/ManualVerification.md` for the editor flow).

## CI (`.github/workflows/ci.yml`)

On push to `main`/`develop` and on every PR: native build + `EngineUnitTests` + headless test in `Debug` and `Release` with `-WarningsAsErrors`; the `Editor build` job runs the file-convention check, `dotnet build` of the editor and `DXForgeImport` with `-WarningsAsErrors`, and the headless script tests listed above.

## Token cost of build output (measured)

The native MSBuild log is ~7,400 characters at default verbosity. `dotnet build` of the editor was **~132,000 characters** while it had 588 nullable warnings (272 unique, printed twice because of the WPF temp project); after issue #9 the raw minimal-verbosity log is ~470 characters. `build.ps1` still summarizes by default so a regression cannot flood a session again. The C4566 failure in a CI log was 43 near-identical lines.
