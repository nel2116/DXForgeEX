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

Build via Visual Studio 2022 (`PlatformToolset v143`, open `DXForge.sln`) or via the CLI: root `build.ps1` wraps MSBuild (native) and `dotnet build` (editor) and can run `EngineUnitTests` afterwards — see `README.md` for the parameter list and examples. **Always build native projects through `DXForge.sln`** (directly invoking a `.vcxproj` makes `$(SolutionDir)` fall back to the project's own directory, breaking the `AdditionalIncludeDirectories` paths — `build.ps1` already does this correctly via `/t:<ProjectName>`). `GetDXC.ps1` fetches the DirectX Shader Compiler package used for shader compilation (`packages/DirectXShaderCompiler`). There is no lint config or CI. `EngineUnitTests` covers only pure-logic modules; rendering/GPU code (`Graphics/Direct3D12/*`) still has to be verified by running `EngineTest` or the editor and visually inspecting the render output.

**New C++ source files must be saved with a UTF-8 BOM.** Every existing file in this repo starts with a Japanese header comment (see below); without a BOM, MSVC misdetects the file as the local codepage (Shift-JIS) instead of UTF-8 and corrupts the token stream, producing unrelated-looking cascading compile errors. If a new file produces confusing errors at/after its first Japanese comment, check for a missing BOM first.

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
