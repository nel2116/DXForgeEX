# Manual verification of the editor

<!-- last-reviewed: 2026-11-07 -->

The editor's basic flow (create a project -> add a script -> load the game code DLL -> run the game) cannot be checked by `EngineUnitTests` or CI. This procedure walks through it by hand (issue #43). Use it after changes to the editor's or `DXForgeEngine`'s runtime behavior, and after toolchain updates (Visual Studio/.NET). Agents run these checks through the `gui-check` skill.

It takes about 10 minutes if everything is built.

Every command here can be pasted as-is into **PowerShell opened at the repository root** (PowerShell 7 or Windows PowerShell 5.1). Variables defined in step 2 (`$verifyName` etc.) are used again in the cleanup of step 7, so stay in the same PowerShell window until the end.

Japanese strings in code blocks and quotes are the editor's actual messages; they are kept verbatim so they can be matched.

## 1. Prerequisites

### Environment

- Windows 10/11, a DirectX 12 GPU
- Visual Studio 2022 (`v143`) with the workloads "Desktop development with C++" and ".NET desktop development", plus the individual component **"C++ ATL"** (for `atlsafe.h`; without it `DXForgeEngineDLL` and game projects do not build)
- .NET 8 SDK or later
- **Autodesk FBX SDK 2020.3.7** (needed to build `ContentTools`; if it is not in the default location, set `FbxSdkDir`; see `docs/dev/BuildAndTest.md`)
- The editor starts Visual Studio 2022 through DTE (COM) to build and run game projects, so Visual Studio windows may open during the check.

### What to build

At the repository root:

```powershell
# 1) Editor configuration: DXForgeEngine (lib), DXForgeEngineDLL, ContentTools
./build.ps1 -Configuration DebugEditor -Target native

# 2) The editor (output: x64\DebugEditor\DXForgeEditor.exe)
./build.ps1 -Target editor

# 3) The engine lib in the standalone configuration (x64\Debug\DXForgeEngine.lib)
./build.ps1 -Configuration Debug -Target native -Project DXForgeEngine
```

Why 3) is needed: running the game from the editor (F5/Ctrl+F5) builds the game project in the standalone `Debug` configuration (an executable), which links `$(DXFORGE_ENGINE)x64\Debug\DXForgeEngine.lib`. Without it, LoggerView shows 「Build: Debug configuration 失敗」 and the game does not start (Visual Studio's output: `LNK1104: ファイル 'DXForgeEngine.lib' を開くことができません`). `EngineTest` and `EngineUnitTests` are not needed; building `DXForgeEngine` alone is enough (measured 2026/10/18).

In a fresh clone/worktree `packages/` is empty, but `build.ps1` restores it automatically through `Restore.ps1`.

### The `DXFORGE_ENGINE` environment variable

Game projects find the engine's headers and libs through the user environment variable `DXFORGE_ENGINE` (the engine repository root, with a trailing `\`). If it is unset, the editor asks for it in a dialog on first start.

**Always check that it points at the checkout (worktree) you want to verify.** If it points elsewhere, that checkout's lib is linked and your change is not tested.

```powershell
[Environment]::GetEnvironmentVariable("DXFORGE_ENGINE", "User")
# To switch to this checkout temporarily (keep the old value in $origEngine; restored in step 7)
$origEngine = [Environment]::GetEnvironmentVariable("DXFORGE_ENGINE", "User")
[Environment]::SetEnvironmentVariable("DXFORGE_ENGINE", "$PWD\", "User")
```

The editor reads the user environment variable `DXFORGE_ENGINE` at startup (from the registry, so the environment of the shell that started it does not matter); change it before starting the editor.

The Visual Studio that the editor starts through DTE (COM) is a child of the DcomLaunch service (`svchost.exe`) and **inherits the environment block from logon**. A user environment variable changed after logon does not reach the Visual Studio process even if it is restarted with no devenv running (measured in #102; it keeps the old value until sign-out). So before building the game, the editor writes `Directory.Build.props` into the solution folder (next to `<Name>.sln`) and passes the `DXFORGE_ENGINE` it read as an MSBuild property (project properties win over environment variables). A `Directory.Build.props` in a parent folder is imported too. If that folder already has a `Directory.Build.props` the editor did not create, it is not overwritten and LoggerView shows a warning; the Visual Studio process's environment variable is used then.

Which lib was linked can be checked after the game's `Debug` build (F5/Ctrl+F5 in step 6) in the linker's read log (`$verifyRoot`/`$verifyName` are set in step 2):

```powershell
Get-Content "$verifyRoot\$verifyName\GameCode\x64\Debug\$verifyName.tlog\link.read.1.tlog" -Encoding Unicode | Select-String DXFORGEENGINE.LIB
# e.g. C:\...\<this worktree>\X64\DEBUG\DXFORGEENGINE.LIB
```

## 2. Create/open a project

1. Put the check project's name and location into variables and start the editor. The Project Browser opens.

```powershell
# Name of the check project and the folder it is created in (expanded absolute path); also used in step 7
$verifyName = "Verify43"
$verifyRoot = Join-Path $env:TEMP "DXForgeVerify"
# Put it on the clipboard to paste into the Path field below
Set-Clipboard "$verifyRoot\"
"$verifyRoot\"
# The editor reads templates from next to its executable (x64\DebugEditor\ProjectTemplates), so any working directory works
# (the test output of texture loading, <repo>\x64\texture.img, is also located from the executable; #91)
Start-Process -FilePath "$PWD\x64\DebugEditor\DXForgeEditor.exe"
```

2. Choose "Create Project", template `Empty Project`, Name = the value of `$verifyName` (`Verify43`), Path = the value of `$verifyRoot\`, then "Create".
   - Select everything in the Path field (Ctrl+A) and paste (Ctrl+V). Typing the value shown above (e.g. `C:\Users\<user>\AppData\Local\Temp\DXForgeVerify\`) also works.
   - Environment variables in the Path field are expanded, so `%TEMP%\DXForgeVerify\` creates it in the same place (after "Create" the field shows the expanded absolute path). A relative path (e.g. `DXForgeVerify\`) is rejected with 「プロジェクトパスには絶対パスを入力してください」.
   - The default Path is the user environment variable `DXFORGE_PROJECTS` if set, else `%USERPROFILE%\DXForgeProjects\` (#141; if the value is unusable, LoggerView shows a Warning and the latter is used). A dedicated folder makes cleanup easier.
   - If the previous check's folder (`$verifyRoot\$verifyName\`) is still there, creation fails with 「選択したプロジェクトフォルダはすでに存在しています」. Do the cleanup of step 7 first.

The editor build (`./build.ps1 -Target editor`) copies the templates from `DXForgeEditor\ProjectTemplates` to `x64\DebugEditor\ProjectTemplates`; rebuild the editor after editing a template. If no template is found, the new-project list is empty and LoggerView shows 「プロジェクト・テンプレートの読み込みに失敗しました: <path>」.

**Expected**

- The world editor (title "DXForge Editor") opens and Hierarchy has "Sample Scene".
- `<Path>\<Name>\` contains `<Name>.dxproj`, `<Name>.sln`, `GameCode\<Name>.vcxproj` and so on.
- A build runs automatically on open and LoggerView shows these three lines. The third (a yellow warning) is **expected**: an empty project has no sources, so no DLL is produced.

```
Build: GameCode\<Name>.vcxproj, DebugEditor, x64, DebugEditor|x64
Build: DebugEditor configuration 成功
ゲームコードDLLファイルのロードに失敗しました。 まずプロジェクトをビルドしてみてください！   (warning, yellow)
```

Existing projects open from the Project Browser's "Open Project" list with "Open".

## 3. Add a script

1. Press "New Script" in the toolbar.
2. Enter a Script name (e.g. `SpinScript`), leaving Path at the default `GameCode\`. The dialog shows 「`SpinScript.h と SpinScript.cpp が <Name> に追加されます`」.
3. Press "Create".

**Expected**

- `GameCode\SpinScript.h` and `GameCode\SpinScript.cpp` are created (`REGISTER_SCRIPT(SpinScript);` and empty `begin_play()`/`update(float dt)`).
- A Visual Studio window opens (in front), the two files are added to the `<Name>` project in Solution Explorer, and `SpinScript.cpp` opens in the editor.

**Note:** at this point the files are added only in Visual Studio's memory, not yet to the `GameCode\<Name>.vcxproj` file. It is saved during the build in the next step, which adds `<ClCompile Include="SpinScript.cpp" />`/`<ClInclude Include="SpinScript.h" />`. Check the vcxproj after step 4.

**(Optional) a probe showing the script ran:** to see in step 6 that the script executed, have `update()` append a line to a file. For example:

```cpp
#include <fstream>
// ...
namespace { int frames{ 0 }; }
void SpinScript::update(float dt)
{
    if (++frames % 60 == 0)
        std::ofstream{ R"(C:\...\<Name>\script_log.txt)", std::ios::app } << "update frames=" << frames << " dt=" << dt << "\n";
}
```

When running with F5 (with debugging), a breakpoint in `update()` works too.

## 4. Build and load the game code DLL

1. Back in the editor, press "Build" in the toolbar (or F7).

**Expected**

- `<Path>\<Name>\x64\DebugEditor\<Name>.dll` is produced.
- LoggerView shows the following, **without the DLL load warning**:

```
Build: GameCode\<Name>.vcxproj, DebugEditor, x64, DebugEditor|x64
Build: DebugEditor configuration 成功
ゲームコードDLLは正常にロードされました。
```

- `<Name>.dll` is loaded in the editor process (`tasklist /M <Name>.dll` lists `DXForgeEditor.exe`).

There is one "Build: ..." start line per build. A former bug (#48) added one more line per build (two on the second build, three on the third).

## 5. Attach the script

1. Press "+" on "Sample Scene" in Hierarchy to add an entity ("Empty Entity").
2. Select it. The inspector shows Name/Enabled and Transform (Position/Rotation/Scale).
3. Press "Add Component" in the inspector and hover over "Script".

**Expected**

- The "Script" submenu lists the script added in step 3 (`SpinScript`). The list comes from the loaded game code DLL through the engine's `get_script_names`; without a loaded DLL, "Script" is disabled.
- Choosing `SpinScript` adds a Script component (Name: `SpinScript`) to the inspector.
- Choosing "Geometry" adds a Geometry component with the default geometry (`DefaultCube`) (#32). Physics is disabled because it is not implemented (#20).

A new entity's Transform shows Position `0, 0, 0`, Rotation `0, 0, 0`, Scale `1, 1, 1` (fixed in #49; before that Scale was `0, 0, 0` too).

## 6. Run the game

The combo box on the right of the toolbar (default `Debug`) is the standalone configuration built for running. Running and stopping the game is **only possible with keyboard shortcuts** (no menu or button). They work while the world editor has focus (click Hierarchy or similar first).

| Action | Key | Behavior |
|---|---|---|
| Run with debugging | F5 | Build `Debug` -> write `game.bin` -> Visual Studio `Debug.Start` |
| Run without debugging | Ctrl+F5 | Same, `Debug.StartWithoutDebugging` |
| Stop | Shift+F5 | Visual Studio `Debug.StopDebugging` |

**Expected**

- LoggerView shows:

```
Build: GameCode\<Name>.vcxproj, Debug, x64, Debug|x64
Build: Debug configuration 成功
```

- `<Path>\<Name>\x64\Debug\<Name>.exe` and **`x64\Debug\game.bin`, written by `SaveToBinary` right before running**, exist (modified at the run time). With one Empty Entity + a Script component it is 70 bytes and ends with the script name (`SpinScript`). The game reads `game.bin` with the executable's directory as the current directory.
- A game window titled "DXForge Game" opens (it shows only the background for now).
- The script's `update()` is called every frame (with the probe from step 3, `script_log.txt` gets `update frames=60 dt=10`... appended).
- With F5, Visual Studio's title becomes 「`<Name> (実行中)`」. Shift+F5 ends the game process and the title returns.

**Notes:**

- `begin_play()` is not called yet, a known bug (#47). Use `update()` to check scripts.
- **A game started with Ctrl+F5 (no debugging) does not stop with Shift+F5**, because Visual Studio is not attached; this is Visual Studio's own behavior. Close the game window instead.
- Without `x64\Debug\DXForgeEngine.lib` this fails with 「Build: Debug configuration 失敗」 (see 3) in the prerequisites).

## 7. Cleanup

If you clean up from another PowerShell window, first run the two lines defining `$verifyName` and `$verifyRoot` from step 2 again (`$origEngine` is lost, so give the old value directly in step 4 below).

1. Close the editor (the window's x). Unloading the project also closes the Visual Studio the editor started. Confirm no `DXForgeEditor.exe` or `devenv.exe` is left (except a Visual Studio you opened yourself).

```powershell
tasklist /FI "IMAGENAME eq DXForgeEditor.exe"
tasklist /FI "IMAGENAME eq devenv.exe"
```

2. Delete the check project's folder.

```powershell
Remove-Item -Recurse -Force $verifyRoot
```

3. Delete only the check project's entry from `%APPDATA%\DXForgeEditor\ProjectData.xml`. This file is the Project Browser's "Open Project" list, where created/opened projects are registered. Deleting the folder leaves the entry, so remove just that `<ProjectData>` element without touching others. The editor identifies projects by `ProjectPath` + `ProjectName` (full path), so to avoid deleting a same-named project elsewhere, delete only entries where **both `ProjectName` and `ProjectPath` match**.

First take a backup and set the `ProjectPath` to delete. The backup gets a timestamped name each time, so it is never confused with or overwritten by an earlier one.

```powershell
$f = "$env:APPDATA\DXForgeEditor\ProjectData.xml"
$bak = "$f.$(Get-Date -Format yyyyMMdd-HHmmss).bak"
Copy-Item $f $bak
# The editor registers the Path field (environment variables expanded) + project name + "\" as ProjectPath
$projectPath = "$verifyRoot\$verifyName\"
```

Then delete the entries where both `ProjectName` and `ProjectPath` match.

```powershell
[xml]$x = Get-Content $f -Raw -Encoding UTF8
$ns = New-Object Xml.XmlNamespaceManager $x.NameTable
$ns.AddNamespace("d", "http://schemas.datacontract.org/2004/07/DXForgeEditor.GameProject")
$targets = @($x.SelectNodes("//d:ProjectData", $ns) | Where-Object { $_.ProjectName -eq $verifyName -and $_.ProjectPath -eq $projectPath })
"Entries to delete: $($targets.Count) ($projectPath)"
foreach ($e in $targets) { [void]$e.ParentNode.RemoveChild($e) }
$x.Save($f)
```

If it reports 0 entries, a value other than `$verifyRoot\` was entered in the Path field in step 2. Open `ProjectData.xml`, put that entry's `ProjectPath` into `$projectPath` (e.g. `$projectPath = "D:\Work\Verify43\"`), and run **only the second code block** again (running the first resets `$projectPath`).

Finally, list the remaining entries and confirm the check entry is gone and the others remain.

```powershell
$x.SelectNodes("//d:ProjectData", $ns) | ForEach-Object { "$($_.ProjectName)  $($_.ProjectPath)" }
```

If all is well, delete the backup made above (so no old backup is left for the next check). If an unintended entry disappeared, keep it and restore with `Copy-Item $bak $f`.

```powershell
Remove-Item $bak
```

4. If you switched `DXFORGE_ENGINE` temporarily, restore it.

```powershell
# $origEngine is the value saved under "switch temporarily" in step 1; nothing happens if you did not switch
if (Test-Path variable:origEngine) { [Environment]::SetEnvironmentVariable("DXFORGE_ENGINE", $origEngine, "User") }
[Environment]::GetEnvironmentVariable("DXFORGE_ENGINE", "User")
```

Starting the editor again, the Project Browser list no longer has the check project and still has the others.

## Checking viewport rendering (Primal Day 083, #209)

Start from a project opened as in step 2. Object rendering comes in Day 084, so in Day 083 the frame is empty.

1. **It renders**: judge by both of these. The viewport is not WPF's navy background RGB(26,48,76) (expected: DebugEditor's gray clear color; with IBL the sky shows). The toolbar's `FPS` is a number (near the refresh rate) and `DT` keeps updating. Also read `Frames: N` twice with `gui.ps1 -Action tree -ProcId <PID> -Pattern '^Text'` and check that the difference divided by the elapsed seconds is close to `FPS` (the display updates every second, so wait 10 seconds or more; `N` is the running count of frames actually drawn. `FPS` also counts when nothing is drawn, so use `Frames` to judge that rendering happens).
2. **Input**: mouse drag, wheel and the `F` key over the viewport do not crash. The camera is not connected to the engine yet, so **the picture not changing is correct**.
3. **UI**: the hierarchy panel and menus respond while rendering. The window does not hang while its edge is dragged to resize. `FPS` keeps updating after resizing.
4. **Switching and exit**: switch projects twice from the `File` menu, then close the editor; no crash and no Windows error dialog. Simulate logoff with `gui.ps1 -Action end-session -ProcId <PID>` with a project open, with the Project Browser at startup and with the `File > Open Project` Project Browser; the exit code is 0 in every case (no real logoff happens).
5. **CPU**: measure the difference of `Get-Process DXForgeEditor`'s `CPU` seconds over 10 seconds each. With one logical core = 100%, take the Project Browser open (no render thread) as the baseline and compare: project open, minimized, and the viewport minimized with the splitter. Under one logical core passes; investigate if it rises more than 50% over the baseline.

## Checking the number boxes (Primal Day 086, #244)

The inspector's Transform rows and the Primitive mesh dialog's size boxes are `NumberBox`es (`docs/design/number-box.md`). The value logic and events have a headless test (`number_box_test.ps1`); check the mouse and keyboard handling here.

1. **Drag**: press on a Transform box and drag left/right; the value changes while dragging (faster with Shift, slower with Ctrl) and the cursor is the left-right arrow. Releasing leaves the value. One drag is one `Edit > Undo` step, and undo/redo restore the values before/after the drag. The drag takes the keyboard focus: pressing Delete right after it does not delete the entity selected in the Hierarchy.
2. **Type**: click without moving; the box becomes editable with the text selected. Enter, Tab or clicking elsewhere commits (one undo step); Escape discards the typing. Text that is not a number is rejected and the old value comes back.
3. **Mixed values**: select two entities with different positions; the differing boxes are empty with a gray background. Typing or dragging there sets all selected entities to the same value, and undo restores each one's own value.
4. **Primitive mesh dialog**: dragging a size box updates the preview while dragging; typing a value updates it on commit.

## Verification record

### 2026/10/18 (issue #43)

With #44, #45 and #46 merged (`origin/develop` + the three branches merged locally), the procedure above was run to the end from a new worktree. The GUI was operated with Orca's `orca computer` (UI Automation), and F5/Ctrl+F5/Shift+F5 were sent with `WScript.Shell`'s `SendKeys`. The current tool is `gui.ps1` of the `gui-check` skill (#195).

| Step | Result | What was checked |
|---|---|---|
| 1. Prerequisites | OK | `DebugEditor` native/editor builds with 0 warnings. Confirmed that F5 fails with LNK1104 without `x64\Debug\DXForgeEngine.lib`, fixed by `-Configuration Debug -Project DXForgeEngine` alone |
| 2. Create/open | OK | 「Build: DebugEditor configuration 成功」 + the DLL load warning (expected) |
| 3. New Script | OK | `.h`/`.cpp` generated and added to the Visual Studio project (saved to the vcxproj at build time) |
| 4. Build | OK | `x64\DebugEditor\Verify43.dll`, 「ゲームコードDLLは正常にロードされました。」, `Verify43.dll` loaded in `DXForgeEditor.exe` |
| 5. Attach | OK | `SpinScript` in the Script submenu, Script component in the inspector |
| 6. Run/stop | OK (known bug) | With both Ctrl+F5 and F5 the "DXForge Game" window opened and `update()` was called. `game.bin` (70 bytes) was updated right before the run. The F5 run stopped with Shift+F5. `begin_play()` is not called (#47) |
| 7. Cleanup | OK | Folder deleted, only the matching `ProjectData.xml` entry deleted, `DXFORGE_ENGINE` restored |

Bugs found and filed then:

- #47 The engine never calls a script's `begin_play()`
- #48 Every editor build registers the DTE build event handler again, so log lines keep multiplying
- #49 The Transform Scale of an entity added in the editor is written to `game.bin` as 0,0,0

## Automation notes

### Reproducing with UI Automation

Steps 2-7 could all be operated with Orca's `orca computer` (UI Automation on Windows), and LoggerView's text was readable from the accessibility tree, so scripting the procedure is possible. These points needed care:

- **Some WPF ToggleButtons do not react to UIA Toggle/Invoke.** The Project Browser's "Create Project" and the inspector's "Add Component" (opened on `PreviewMouseLeftButtonDown`) did not switch on an element click and needed a click by coordinates. A text box's `SetValue` is also rejected ("cannot set value") until the view switches. Giving elements `AutomationProperties.AutomationId` and routing actions through commands makes it stable.
- **Function keys (F5/F7) cannot be sent with `orca computer`'s key input** (`Unsupported key: F5`). The window was brought to the front and `SendKeys` used instead. Run/stop buttons in the toolbar would make UIA alone enough.
- In some cases (e.g. Ctrl+F5) Visual Studio builds without showing its window (`MainWindow.Visible = showWindow`), so failure output cannot be read through UIA. To investigate, build the generated `.sln` directly with `MSBuild <Name>.sln /p:Configuration=Debug /p:Platform=x64` (after the editor built once, the engine path in the `Directory.Build.props` it wrote is used).
- Pass/fail can be judged mechanically from LoggerView strings, the presence and timestamps of outputs (`<Name>.dll`, `<Name>.exe`, `game.bin`), `tasklist /M <Name>.dll`, and the files scripts write.

### Parts to move to headless driving (#11)

Only the editor UI itself (Project Browser, inspector, menus) truly needs the GUI. As #11 (headless engine driving) progresses, these can be checked without the GUI, in CI too:

- **Loading the game code DLL and getting script names**: call the engine DLL's `LoadGameCodeDll`/`GetScriptNames` (which calls `get_script_names` exported by the game code DLL) on a test project's DLL.
- **Loading `game.bin` and running scripts**: run `content::load_game()` -> `script::update()` for a few frames and check how `begin_play()`/`update()` are called (also prevents #47 from recurring). The `game.bin` format pairs with the editor's `SaveToBinary`, so a fixed `game.bin` as test data allows checking without the editor.
- **Building the game project**: building a project generated from the template with `MSBuild` in `DebugEditor`/`Debug` can be checked from the CLI alone without DTE (no Visual Studio start). Template expansion lives in the editor's `NewProject.cs`, though, so a CLI entry point is needed.
