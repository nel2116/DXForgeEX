# DXForgeImport (headless asset importer, issue #10)

C#/.NET console app, built only in `DebugEditor`/`ReleaseEditor` (like the editor).

```
DXForgeImport --input <file> --output <folder> [--type auto|geometry|texture] [--settings import.json] [--set Name=Value] [--json]
```

It writes the same `.asset` files as the editor's drag-and-drop import (FBX → geometry, images → texture; IBL prefiltering and primitive meshes are not exposed yet).

## Design

- It does **not** reimplement the `.asset` format: it `ProjectReference`s `DXForgeEditor` (`InternalsVisibleTo("DXForgeImport")`) and calls the editor's own `Content` code (`ContentHelper.CreateAsset`/`GetAssetName`/`GetAssetFullPath`, `Asset.Import`/`Save`), so the format stays implemented in one place.
- The editor's dependencies on an open project are swapped through `Content/ContentImportEnvironment.cs` (`TempFolder` for the FBX temp copy, `ImportEmbeddedFiles` for `.fbm` embedded textures; both `null` in the editor = unchanged behavior) and `Logger.MessageLogged` (synchronous log event → stderr/JSON).
- WPF is still needed (logger dispatcher, geometry icon rendering via `GeometryView`): the CLI creates a plain `Application` on its STA main thread, merges only `DXForgeEditor;component/Dictionaries/EditorColors.xaml` (the editor's `App` is not used: its `App.xaml` dictionaries use `pack://application:,,,/...`, which resolves against the *entry* assembly, and it has `StartupUri`), never calls `Run()`, and never shuts the dispatcher down (`Logger.Log` is `async void` and awaits `BeginInvoke`; a stopped dispatcher would throw out of it).
- No window, project or GPU window is needed (texture compression creates a windowless D3D11 device and falls back to the CPU).
- Embedded FBX textures are copied from the temp `.fbm` into `<output>/<fbx asset name>.fbm/` before importing, so the texture `.asset`'s `Sources` (used by the editor's reimport) does not point into the deleted temp folder.
- A geometry save is checked against `Geometry.LODGroupCount` and against errors logged during `Save()`, because `Geometry.Save()` catches its own exception and returns only the files written so far (partial save → exit 1, written files stay in `files`).
- Needs `ContentTools.dll` in the same folder (`x64/<config>/`, so FBX SDK is needed to build it).

Exit codes: 0 ok, 1 import/save failed, 2 bad arguments, 3 input missing, 4 unsupported format/type mismatch, 5 bad settings, 6 output folder error, 7 `ContentTools.dll` not loadable, 10 unexpected.

## Build

`build.ps1 -Target editor` builds `DXForgeImport.csproj`, which builds the editor through the reference with the same properties (verified: an injected `CS8602` in the editor still fails `-WarningsAsErrors`).

## Test

`pwsh -STA -NoProfile -File DXForgeImport/Tests/import_cli_test.ps1 [-Configuration ReleaseEditor]` — needs `ContentTools.dll` and the FBX SDK sample `samples/ConvertScene/box.fbx`; also loads the CLI's `.asset` files with the editor's `Texture.Load`/`Geometry.Load` and compares them with the editor's own import path `ContentHelper.Import`. CI runs only `-ArgumentsOnly` (argument/error cases, including exit code 7 since the runner has no `ContentTools.dll`).

- The test builds an embedded-texture FBX on the fly from the ASCII `box.fbx` (a `Video` object with base64 `Content` **plus** a `Video` entry in `Definitions` — without the latter, or without the `Properties70`/`Path`, the FBX SDK does not extract it to `.fbm`).
- `-MultiLodFbxFile <fbx>` enables the partial-save test (no multi-LOD-group FBX in the repo or the SDK samples).
- Pitfall: when comparing texture `.asset` bytes from pwsh, compare the *decompressed* payload — pwsh 7.6 runs on .NET 10, whose Deflate output differs byte-for-byte from .NET 8's (the CLI's and the editor's), so the compressed bytes and their SHA-256 hash differ even though the content is identical.
