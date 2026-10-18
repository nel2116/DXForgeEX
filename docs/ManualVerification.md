# エディタの手動検証手順

エディタの基本操作(プロジェクトの作成 → スクリプトの追加 → ゲームコードのDLLの読み込み → ゲームの実行)は、`EngineUnitTests`やCIでは確認できません。この手順書は、それを手作業で最後まで通すためのものです(Issue #43)。エディタや`DXForgeEngine`の実行時の挙動に関わる変更をしたときや、ツールチェーン(Visual Studio/.NET)を更新したときに使ってください。

所要時間の目安は、ビルド済みなら10分程度です。

この手順書のコマンドは、すべて**リポジトリのルートで開いたPowerShell**(PowerShell 7またはWindows PowerShell 5.1)に、そのままコピーして実行できるように書いています。手順2で定義する変数(`$verifyName`など)を手順7の後片付けでも使うので、最後まで同じPowerShellのウィンドウで続けてください。

## 1. 前提

### 必要な環境

- Windows 10/11、DirectX 12対応GPU
- Visual Studio 2022(`v143`)。ワークロード「C++によるデスクトップ開発」と「.NETデスクトップ開発」に加えて、個別のコンポーネントの**「C++ ATL」**(`atlsafe.h`を使うため。ないと`DXForgeEngineDLL`とゲームのプロジェクトがビルドできません)
- .NET 8 SDK以降
- **Autodesk FBX SDK 2020.3.7**(`ContentTools`のビルドに必要。標準のインストール先以外にある場合は`FbxSdkDir`で指定する。READMEの「CI」の節を参照)
- エディタは、ゲームのプロジェクトのビルドと実行にVisual Studio 2022をDTE(COM)経由で起動します。検証中にVisual Studioのウィンドウが開くことがあります。

### ビルドしておくもの

リポジトリのルートで、次の3つを実行します。

```powershell
# 1) エディタ用の構成: DXForgeEngine(lib)、DXForgeEngineDLL、ContentTools
./build.ps1 -Configuration DebugEditor -Target native

# 2) エディタ本体 (x64\DebugEditor\DXForgeEditor.exe に出力される)
./build.ps1 -Target editor

# 3) スタンドアロン用の構成のエンジンのlib (x64\Debug\DXForgeEngine.lib)
./build.ps1 -Configuration Debug -Target native -Project DXForgeEngine
```

3)が必要な理由: エディタでゲームを実行(F5/Ctrl+F5)すると、ゲームのプロジェクトがスタンドアロン用の`Debug`構成(実行ファイル)でビルドされ、`$(DXFORGE_ENGINE)x64\Debug\DXForgeEngine.lib`をリンクします。これがないと、LoggerViewに「Build: Debug configuration 失敗」と出て、ゲームは起動しません(Visual Studioの出力は`LNK1104: ファイル 'DXForgeEngine.lib' を開くことができません`)。`EngineTest`や`EngineUnitTests`は不要で、`DXForgeEngine`だけをビルドすれば足ります(2026/10/18に実測)。

新しいclone/worktreeでは`packages/`が空ですが、`build.ps1`が`Restore.ps1`で自動的に復元します。

> この手順は、#44(`build.ps1`のエラー要約/`-DetailedLog`)、#45(`Restore.ps1`による`packages/`の自動復元)、#46(エディタを`x64\<構成>\`に出力)がマージされていることを前提にしています。

### 環境変数`DXFORGE_ENGINE`

ゲームのプロジェクトは、ユーザー環境変数`DXFORGE_ENGINE`(エンジンのリポジトリのルート、末尾に`\`)でエンジンのヘッダとlibを探します。エディタの初回起動時に、未設定ならダイアログで設定を求められます。

**検証したいチェックアウト(worktree)を指しているかを必ず確認してください。** 別のチェックアウトを指していると、そちらのlibでビルドされ、検証したい変更が反映されません。

```powershell
[Environment]::GetEnvironmentVariable("DXFORGE_ENGINE", "User")
# 一時的にこのチェックアウトへ切り替える場合(元の値を$origEngineに控え、手順7で戻す)
$origEngine = [Environment]::GetEnvironmentVariable("DXFORGE_ENGINE", "User")
[Environment]::SetEnvironmentVariable("DXFORGE_ENGINE", "$PWD\", "User")
```

Visual StudioはCOM経由で起動されるため、エディタのプロセスの環境変数ではなく、ユーザー環境変数の値が使われます。そのため、変更はエディタを起動する前に行ってください。

## 2. プロジェクトの作成/開く

1. 検証用の名前と場所を変数に入れ、エディタを起動する。Project Browserが開く。

```powershell
# 検証用のプロジェクトの名前と、それを作るフォルダ(展開済みの絶対パス)。手順7でも使う
$verifyName = "Verify43"
$verifyRoot = Join-Path $env:TEMP "DXForgeVerify"
# 手順2でPath欄に貼り付けるため、クリップボードに入れておく
Set-Clipboard "$verifyRoot\"
"$verifyRoot\"
# エディタはテンプレートを作業フォルダからの相対パス(..\..\DXForgeEditor\ProjectTemplates)で読むため、
# 作業フォルダをx64\DebugEditorにして起動する(このPowerShellの場所はリポジトリのルートのまま)
Start-Process -FilePath "$PWD\x64\DebugEditor\DXForgeEditor.exe" -WorkingDirectory "$PWD\x64\DebugEditor"
```

2. 「Create Project」を選び、テンプレート`Empty Project`、Nameに`$verifyName`の値(`Verify43`)、Pathに`$verifyRoot\`の値を入力して「Create」。
   - Path欄は、中身を全部選択(Ctrl+A)してから貼り付け(Ctrl+V)ます。上のコマンドで表示した値(例: `C:\Users\<ユーザー名>\AppData\Local\Temp\DXForgeVerify\`)を手で入力してもかまいません。
   - **`%TEMP%`のような環境変数をPath欄に書かないでください。** エディタは環境変数を展開しないので、作業フォルダ(`x64\DebugEditor`)の下に`%TEMP%`という名前のフォルダが作られ、手順7の後片付けの対象からも外れます。
   - 既定のPathは`ドキュメント\DXForgeProjects\`です。後片付けを簡単にするため、専用のフォルダを使うことをおすすめします。
   - 前回の検証のフォルダ(`$verifyRoot\$verifyName\`)が残っていると「選択したプロジェクトフォルダはすでに存在しています」と出て作成できません。手順7の後片付けを先に行ってください。

**注意:** リポジトリのルートで`.\x64\DebugEditor\DXForgeEditor.exe`のように直接起動すると、作業フォルダがリポジトリのルートになり、テンプレートが見つかりません。必ず上の`Start-Process`(または`x64\DebugEditor`に移動してから起動)を使ってください。

**期待される結果**

- ワールドエディタ(タイトル「DXForge Editor」)が開き、Hierarchyに「Sample Scene」がある。
- `<Path>\<Name>\`に`<Name>.dxproj`、`<Name>.sln`、`GameCode\<Name>.vcxproj`などができる。
- 開いたときに自動でビルドが走り、LoggerViewに次の3行が出る。3行目の警告は、空のプロジェクトにはソースがなくDLLが作られないためで、**想定どおり**です。

```
Build: GameCode\<Name>.vcxproj, DebugEditor, x64, DebugEditor|x64
Build: DebugEditor configuration 成功
ゲームコードDLLファイルのロードに失敗しました。 まずプロジェクトをビルドしてみてください！   (警告・黄色)
```

既存のプロジェクトは、Project Browserの「Open Project」の一覧から選んで「Open」で開けます。

## 3. スクリプトの追加

1. ツールバーの「New Script」を押す。
2. Script nameに名前(例: `SpinScript`)を入力する。Pathは既定の`GameCode\`のまま。ダイアログに「`SpinScript.h と SpinScript.cpp が <Name> に追加されます`」と表示される。
3. 「Create」を押す。

**期待される結果**

- `GameCode\SpinScript.h`と`GameCode\SpinScript.cpp`が作られる(`REGISTER_SCRIPT(SpinScript);`と、`begin_play()`/`update(float dt)`の空の実装)。
- Visual Studioのウィンドウが開き(前面に出る)、ソリューションエクスプローラーの`<Name>`プロジェクトに2つのファイルが追加され、`SpinScript.cpp`がエディタで開かれる。

**注意:** この時点では、追加はVisual Studioのメモリ上だけで、`GameCode\<Name>.vcxproj`のファイルにはまだ書き込まれていません。次の手順のビルドのときに保存され、`<ClCompile Include="SpinScript.cpp" />`/`<ClInclude Include="SpinScript.h" />`が入ります。vcxprojを直接見て確認する場合は、手順4のあとに確認してください。

**(任意)スクリプトが動いたことを確かめる仕掛け:** 手順6でスクリプトが実行されたことを目で確認できるように、`update()`に、ファイルに1行書き出す処理を入れておくと便利です。例:

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

F5(デバッグあり)で実行するなら、代わりに`update()`にブレークポイントを置いても確認できます。

## 4. ゲームコードのDLLのビルドと読み込み

1. エディタに戻り、ツールバーの「Build」を押す(またはF7)。

**期待される結果**

- `<Path>\<Name>\x64\DebugEditor\<Name>.dll`ができる。
- LoggerViewに次のように出て、**DLL読み込みの警告が出ない**。

```
Build: GameCode\<Name>.vcxproj, DebugEditor, x64, DebugEditor|x64
Build: DebugEditor configuration 成功
ゲームコードDLLは正常にロードされました。
```

- エディタのプロセスに`<Name>.dll`が読み込まれている(`tasklist /M <Name>.dll`で`DXForgeEditor.exe`が出る)。

「Build: …」の開始の行は、ビルド1回につき1行です。以前は、ビルドのたびに1行ずつ増えていく不具合(#48)がありました(2回目のビルドで2行、3回目で3行)。

## 5. スクリプトのアタッチ

1. Hierarchyの「Sample Scene」の「+」を押して、エンティティ(「Empty Entity」)を追加する。
2. 追加したエンティティを選択する。インスペクタにName/EnabledとTransform(Position/Rotation/Scale)が表示される。
3. インスペクタの「Add Component」を押し、「Script」にマウスを合わせる。

**期待される結果**

- 「Script」のサブメニューに、手順3で追加したスクリプト(`SpinScript`)が出る。この一覧は、読み込んだゲームコードのDLLからエンジン側の`get_script_names`経由で取得したものです。DLLが読み込まれていないと「Script」が無効になります。
- `SpinScript`を選ぶと、インスペクタにScriptコンポーネント(Name: `SpinScript`)が追加される。
- Geometry/Physicsは未実装のため無効(#20)。

追加したエンティティのTransformは、Positionが`0, 0, 0`、Rotationが`0, 0, 0`、Scaleが`1, 1, 1`で表示される(#49で修正。それ以前はScaleも`0, 0, 0`だった)。

## 6. ゲームの実行

ツールバーの右側のコンボボックス(既定は`Debug`)が、実行時にビルドするスタンドアロン用の構成です。ゲームの実行と停止は、**キーボードショートカットでのみ**行えます(メニューやボタンはありません)。ショートカットは、ワールドエディタにフォーカスがあるときに効きます(Hierarchyなどをクリックしてから押す)。

| 操作 | キー | 動作 |
|---|---|---|
| 実行(デバッグあり) | F5 | `Debug`構成でビルド → `game.bin`を書き出し → Visual Studioの`Debug.Start` |
| 実行(デバッグなし) | Ctrl+F5 | 同上、`Debug.StartWithoutDebugging` |
| 停止 | Shift+F5 | Visual Studioの`Debug.StopDebugging` |

**期待される結果**

- LoggerViewに次のように出る。

```
Build: GameCode\<Name>.vcxproj, Debug, x64, Debug|x64
Build: Debug configuration 成功
```

- `<Path>\<Name>\x64\Debug\<Name>.exe`と、**実行の直前に`SaveToBinary`が書き出した`x64\Debug\game.bin`**ができる(更新日時が実行した時刻になる)。Empty Entity 1個 + Scriptコンポーネントの場合は70バイトで、末尾にスクリプト名(`SpinScript`)が入っている。ゲームは実行ファイルのディレクトリをカレントにして`game.bin`を読み込みます。
- タイトル「DXForge Game」のゲームのウィンドウが開く(今はまだ何も描画しないので、背景だけのウィンドウ)。
- スクリプトの`update()`が毎フレーム呼ばれる(手順3の仕掛けを入れた場合は`script_log.txt`に`update frames=60 dt=10`…が追記されていく)。
- F5の場合、Visual Studioのタイトルが「`<Name> (実行中)`」になる。Shift+F5でゲームのプロセスが終了し、タイトルが元に戻る。

**注意:**

- `begin_play()`は今のところ呼ばれません。既知の不具合(#47)です。スクリプトの確認には`update()`を使ってください。
- **Ctrl+F5(デバッグなし)で起動したゲームは、Shift+F5では止まりません。** Visual Studioがプロセスにアタッチしていないためで、Visual Studio自体の動作と同じです。ゲームのウィンドウを閉じて終了してください。
- `x64\Debug\DXForgeEngine.lib`がないと、ここで「Build: Debug configuration 失敗」になります(前提の3)を参照)。

## 7. 後片付け

別のPowerShellのウィンドウで後片付けをする場合は、先に手順2の`$verifyName`と`$verifyRoot`の2行を実行して、変数を定義し直してください(`$origEngine`は失われているので、手順4では元の値を直接指定します)。

1. エディタを閉じる(ウィンドウの×)。プロジェクトのアンロード時に、エディタが起動したVisual Studioも閉じられます。`DXForgeEditor.exe`と`devenv.exe`が残っていないことを確認します(自分で別に開いているVisual Studioは除く)。

```powershell
tasklist /FI "IMAGENAME eq DXForgeEditor.exe"
tasklist /FI "IMAGENAME eq devenv.exe"
```

2. 検証用のプロジェクトのフォルダを削除する。

```powershell
Remove-Item -Recurse -Force $verifyRoot
```

3. `%APPDATA%\DXForgeEditor\ProjectData.xml`から、検証用のプロジェクトのエントリだけを削除する。このファイルはProject Browserの「Open Project」の一覧で、作成/オープンしたプロジェクトが登録されます。フォルダを消しても一覧のエントリは残るので、ほかのエントリには触らず、該当の`<ProjectData>`要素だけを消します。エディタはプロジェクトを`ProjectPath`+`ProjectName`(フルパス)で区別するので、別の場所にある同じ名前のプロジェクトを消さないよう、**`ProjectName`と`ProjectPath`の両方が一致するもの**だけを消します。先にバックアップを取ってください。

まず、バックアップを取り、消すエントリの`ProjectPath`を決めます。バックアップは、まだないときだけ作ります(再実行しても、最初のバックアップが上書きされないように)。

```powershell
$f = "$env:APPDATA\DXForgeEditor\ProjectData.xml"
if (-not (Test-Path "$f.bak")) { Copy-Item $f "$f.bak" }
# エディタは、Path欄の値 + プロジェクト名 + "\" をProjectPathとして登録する
$projectPath = "$verifyRoot\$verifyName\"
```

次に、`ProjectName`と`ProjectPath`の両方が一致するエントリを消します。

```powershell
[xml]$x = Get-Content $f -Raw -Encoding UTF8
$ns = New-Object Xml.XmlNamespaceManager $x.NameTable
$ns.AddNamespace("d", "http://schemas.datacontract.org/2004/07/DXForgeEditor.GameProject")
$targets = @($x.SelectNodes("//d:ProjectData", $ns) | Where-Object { $_.ProjectName -eq $verifyName -and $_.ProjectPath -eq $projectPath })
"削除するエントリ: $($targets.Count)件 ($projectPath)"
foreach ($e in $targets) { [void]$e.ParentNode.RemoveChild($e) }
$x.Save($f)
```

削除するエントリが0件と表示された場合は、手順2でPath欄に`$verifyRoot\`以外の値を入力しています。`ProjectData.xml`を開き、該当するエントリの`ProjectPath`の値を`$projectPath`に入れて(例: `$projectPath = "D:\Work\Verify43\"`)から、**2つ目のコードブロックだけ**をもう一度実行してください(1つ目を実行すると`$projectPath`が元の値に戻ります)。

4. `DXFORGE_ENGINE`を一時的に切り替えた場合は、元の値に戻す。

```powershell
# $origEngineは手順1の「一時的に切り替える場合」で控えた値。切り替えていなければ何もしない
if (Test-Path variable:origEngine) { [Environment]::SetEnvironmentVariable("DXFORGE_ENGINE", $origEngine, "User") }
[Environment]::GetEnvironmentVariable("DXFORGE_ENGINE", "User")
```

エディタをもう一度起動して、Project Browserの一覧から検証用のプロジェクトが消え、ほかのプロジェクトが残っていることを確認できます。

## 検証記録

### 2026/10/18(Issue #43)

#44・#45・#46をマージした状態(`origin/develop` + 3ブランチをローカルでマージ)で、新しいworktreeから上の手順を最後まで通しました。GUIの操作はOrcaの`orca computer`(UI Automation)で行い、F5/Ctrl+F5/Shift+F5は`WScript.Shell`の`SendKeys`で送りました。

| 手順 | 結果 | 確認したこと |
|---|---|---|
| 1. 前提 | OK | `DebugEditor`のnative/editorのビルドは警告0件。`x64\Debug\DXForgeEngine.lib`がないとF5でLNK1104になることを実測し、`-Configuration Debug -Project DXForgeEngine`だけで解消 |
| 2. 作成/開く | OK | 「Build: DebugEditor configuration 成功」+ DLL読み込みの警告(想定どおり) |
| 3. New Script | OK | `.h`/`.cpp`の生成、Visual Studioのプロジェクトへの追加(vcxprojへの保存はビルドのとき) |
| 4. Build | OK | `x64\DebugEditor\Verify43.dll`、「ゲームコードDLLは正常にロードされました。」、`DXForgeEditor.exe`に`Verify43.dll`が読み込まれている |
| 5. アタッチ | OK | Scriptのサブメニューに`SpinScript`、インスペクタにScriptコンポーネント |
| 6. 実行/停止 | OK(既知の不具合あり) | Ctrl+F5/F5ともに「DXForge Game」のウィンドウが開き、`update()`が呼ばれた。`game.bin`(70バイト)が実行の直前に更新された。F5の実行はShift+F5で停止できた。`begin_play()`は呼ばれない(#47) |
| 7. 後片付け | OK | フォルダの削除、`ProjectData.xml`の該当エントリだけの削除、`DXFORGE_ENGINE`を元の値に戻した |

このとき見つけて起票した不具合:

- #47 スクリプトの`begin_play()`がエンジンから一度も呼ばれない
- #48 エディタでビルドするたびにDTEのビルドイベントのハンドラが重複登録され、ログの行が増え続ける
- #49 エディタで追加したエンティティのTransformのScaleが0,0,0のまま`game.bin`に書き出される

## 自動化の検討

### UI Automationでの再現

今回、手順2〜7はすべてOrcaの`orca computer`(Windowsでは UI Automation)で操作でき、LoggerViewのテキストもアクセシビリティツリーから読み取れました。そのため、手順書に沿ったスクリプト化は可能です。ただし、次の点に手当てが必要です。

- **WPFのToggleButtonの一部はUIAのToggle/Invokeに反応しません。** Project Browserの「Create Project」と、インスペクタの「Add Component」(`PreviewMouseLeftButtonDown`で開く)は、要素のクリックでは切り替わらず、座標でのクリックが必要でした。テキストボックスの`SetValue`も、表示が切り替わる前は「値を設定できない」と拒否されます。要素に`AutomationProperties.AutomationId`を付け、操作をコマンドに寄せると安定します。
- **ファンクションキー(F5/F7)は`orca computer`のキー操作で送れません**(`Unsupported key: F5`)。今回はウィンドウを前面に出して`SendKeys`で送りました。ツールバーに実行/停止のボタンがあれば、UIAだけで完結します。
- Ctrl+F5で起動したときなど、Visual Studioはウィンドウを表示しないままビルドする(`MainWindow.Visible = showWindow`)ため、失敗時の出力はUIAでは読めません。原因の調査には、生成された`.sln`を`MSBuild <Name>.sln /p:Configuration=Debug /p:Platform=x64`で直接ビルドするのが早いです。
- 合否の判定は、LoggerViewの文字列、生成物(`<Name>.dll`、`<Name>.exe`、`game.bin`)の有無と更新日時、`tasklist /M <Name>.dll`、スクリプトが書き出すファイル、で機械的にできます。

### ヘッドレス駆動(#11)に寄せる部分

GUIが本質的に必要なのは、エディタのUI(Project Browser、インスペクタ、メニュー)の確認だけです。次の部分は、#11(エンジンのヘッドレス駆動)が進めば、GUIなしでCIでも確認できるようになります。

- **ゲームコードのDLLの読み込みとスクリプト名の取得**: エンジンのDLLの`LoadGameCodeDll`/`GetScriptNames`(ゲームコードのDLLがエクスポートする`get_script_names`を呼ぶ)を、テスト用のプロジェクトのDLLに対して呼ぶだけで確認できる。
- **`game.bin`の読み込みとスクリプトの実行**: `content::load_game()` → `script::update()`を数フレーム回し、`begin_play()`/`update()`の呼ばれ方を確認する(#47の再発防止にもなる)。`game.bin`の形式はエディタの`SaveToBinary`と対になっているので、固定の`game.bin`をテストデータとして持てば、エディタなしで検証できる。
- **ゲームのプロジェクトのビルド**: テンプレートから生成したプロジェクトを`MSBuild`で`DebugEditor`/`Debug`構成でビルドする部分は、DTEを使わずにCLIだけで確認できる(Visual Studioの起動が不要になる)。ただし、テンプレートの展開はエディタの`NewProject.cs`にあるため、CLIからの呼び出し口が必要。
