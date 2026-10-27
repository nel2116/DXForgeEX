# DXForgeEX

[Game Engine Series (TheGameEngineers / Arash Khatami)](https://www.youtube.com/playlist?list=PLU2nPsAdxKWQYxkmQ3TdbLsyc1l2j25XM) と、その実装である [Primal](https://github.com/TheGameEngineers/Primal) を参考に実装している、学習用の3Dゲームエンジン + WPFレベルエディタです。

C++によるプラットフォーム非依存のエンジンコア、C ABIのDLL境界、その上で動くC#/WPFエディタ、オフラインのコンテンツ(FBX/テクスチャ)インポートパイプラインという構成を踏襲しています。現在の実装は、PBRの直接光(GGX/Smith/Schlickフレネル)まで到達しており、IBL(Image Based Lighting)は未実装です。

## 必要環境

- Windows 10/11
- Visual Studio 2022 (ワークロード: 「C++によるデスクトップ開発」+「.NETデスクトップ開発」、`v143` toolset)
- .NET 8 SDK以降 (`dotnet` CLI。`DXForgeEditor`のビルドに使用)
- DirectX 12対応GPU (レンダラーを実際に動かす場合のみ必要。`EngineUnitTests`はGPU/ウィンドウ不要)

## ソリューション構成

`DXForge.sln` は `x64` 固定で4つの構成を持ちます: `Debug` / `DebugEditor` / `Release` / `ReleaseEditor`。`*Editor`構成はエンジンを`DXForgeEngineDLL`としてビルドしエディタから使う構成、無印の`Debug`/`Release`は`EngineTest`/`EngineUnitTests`を単独実行ファイルとしてビルドする構成です。

| プロジェクト | 種類 | 概要 |
|---|---|---|
| `DXForgeEngine` | 静的ライブラリ | エンジン本体 |
| `DXForgeEngineDLL` | DLL | エンジンをエディタに公開するC ABIラッパー |
| `EngineTest` | 実行ファイル | ウィンドウ+GPUを使う対話的なビジュアルテスト。CLI/CIには不向き |
| `EngineUnitTests` | 実行ファイル | GPU/ウィンドウ不要の自動テスト。CLI/CI向け |
| `ContentTools` | DLL | FBX/テクスチャのオフラインインポートパイプライン(エディタから使用) |
| `DXForgeEditor` | WPFアプリ (.NET 8) | レベルエディタ |

## ビルド方法

### Visual Studioから

`DXForge.sln` を開き、任意の構成でビルドしてください。

### CLIから (`build.ps1`)

PowerShell 7+ (`pwsh`) から実行します。

```powershell
# ネイティブ側+エディタ側の両方をビルド (既定構成 DebugEditor)
./build.ps1

# 構成を指定してビルド
./build.ps1 -Configuration Debug

# ネイティブ側だけ、特定プロジェクトだけビルド(最速のフィードバックループ)
./build.ps1 -Target native -Project EngineUnitTests

# エディタ側だけビルド
./build.ps1 -Target editor

# ビルド後に自動テストも実行し、終了コードに反映する
./build.ps1 -Target native -Project EngineUnitTests -RunTests

# 終了コードに加えて、最後に1行のJSON要約も出力する(AIエージェント/CI向け)
./build.ps1 -Target native -Project EngineUnitTests -RunTests -Json
```

| パラメータ | 値 | 既定値 |
|---|---|---|
| `-Configuration` | `Debug` / `DebugEditor` / `Release` / `ReleaseEditor` | `DebugEditor` |
| `-Target` | `native` / `editor` / `all` | `all` |
| `-Project` | `DXForge.sln`内のプロジェクト名(`-Target native`時のみ有効) | 省略時はソリューション全体 |
| `-RunTests` | スイッチ。ビルド後に`EngineUnitTests.exe`と、ヘッドレス実行のテスト(`EngineUnitTests/Tests/headless_run_test.py`、pythonが必要)を実行。`EngineUnitTests`は`Debug`/`Release`構成にしかないため、`*Editor`構成・`-Target editor`・別の`-Project`のときは対応する`Debug`/`Release`構成で`EngineUnitTests`を追加でビルドしてから実行する(古い実行ファイルを実行しないため)。ビルドに失敗したときはテストを実行しない | - |
| `-NoRestore` | スイッチ。ネイティブビルド前の`packages/`の復元(`Restore.ps1`)を省く | - |
| `-WarningsAsErrors` | スイッチ。ネイティブ側とエディタ側の警告をエラーとして扱う(CI用) | - |
| `-Json` | スイッチ。1行のJSON要約を標準出力に出す | - |
| `-DetailedLog` | スイッチ。ビルドツールの出力を全文表示する | - |

**注意:** ネイティブ側は必ず`DXForge.sln`経由でビルドしてください。`.vcxproj`を直接MSBuildに渡すと、`$(SolutionDir)`がプロジェクト自身のディレクトリにフォールバックしてしまい、インクルードパスが崩れてビルドに失敗します(MSBuildの既知の挙動)。`build.ps1`は常に`/t:<プロジェクト名>`で`DXForge.sln`を経由するため、この問題を踏みません。

### `packages/`の復元 (`Restore.ps1`)

`packages/`は`.gitignore`で除外しているため、新しいclone/worktreeでは空です。`build.ps1`はネイティブ側をビルドする前に`Restore.ps1`を呼び、足りないものだけを取得します。手作業でのコピーは不要です。

- **NuGet** (`ContentTools/packages.config`の`directxtex_desktop_win10`、`DXForgeEngineDLL/packages.config`の`Microsoft.Direct3D.DXC`と、`DXForgeEditor`の`PackageReference`): `packages/<id>.<version>`か`DXForgeEditor/obj/project.assets.json`が無ければ`MSBuild DXForge.sln -t:restore -p:RestorePackagesConfig=true`でまとめて復元します(`nuget.exe`は不要)。ネイティブのビルドは*Editor構成でソリューション内の`DXForgeEditor`もビルドするため、`obj`だけを消した状態でも`NETSDK1004`にならないようにしています。いずれも`*Editor`構成でしかビルドされないため、`Debug`/`Release`構成では復元しません。
- **DXC** (`packages/DirectXShaderCompiler`): フォルダが空なら`GetDXC.ps1`で最新版を取得します(GitHub APIを使います。環境変数`GITHUB_TOKEN`か`GH_TOKEN`があれば認証付きで呼び、レート制限の上限が上がります。CIはこれで`GITHUB_TOKEN`を渡しています。無ければ認証なしで呼びます。失敗したときは間をおいて再試行し、レート制限で失敗したときはそうとわかるエラーを出します)。このDXCを使うのは`EngineTest`だけなので、`EngineTest`をビルドするとき(`Debug`/`Release`構成で、`-Project`を省略したときか`EngineTest`を指定したとき)だけ取得します。`-Project EngineUnitTests`や`*Editor`構成のビルドは、オフラインやGitHub APIのレート制限の下でもこのDXCなしで進みます。エディタ側のシェーダのコンパイル(`DXForgeEngineDLL`の`CompileShader`、#29)は、これとは別に上のNuGetの`Microsoft.Direct3D.DXC`(版を固定)を使い、そのtargetsが`dxcompiler.dll`/`dxil.dll`を`DXForgeEngineDLL.dll`と同じ出力先(エディタの実行ファイルと同じ`x64/<構成>/`)にコピーします。`DXForgeEngineDLL.dll`はDXCを実行時に読み込むため、DXCが無くてもエディタは起動でき、シェーダのコンパイルだけが失敗してログに出ます。

既に揃っているときは、ファイルの有無を見るだけでネットワークにはアクセスせず、すぐに終わります(実測: 数十ms)。`Restore.ps1`は単体でも実行でき(`./Restore.ps1 -Configuration DebugEditor`)、`build.ps1 -NoRestore`で復元を省けます。DXCを最新版に更新したいときは、`packages/DirectXShaderCompiler`を削除してから実行してください。

## テスト

`EngineUnitTests` はGPU・ウィンドウを使わない自動テストです。ビルド後、生成された実行ファイルを直接実行できます。

```powershell
./x64/Debug/EngineUnitTests.exe          # 人間が読める出力
./x64/Debug/EngineUnitTests.exe --json   # 機械可読なJSON出力
./x64/Debug/EngineUnitTests.exe --interactive   # assert時にCRT既定のダイアログを出す(デバッガで調べたいとき)
```

終了コードは全テストPASSなら`0`、1件以上FAILなら`1`です。各失敗には式・ファイル・行番号が出力されます。

無人実行で止まらないように、テスト対象のコードが`assert`に当たったり`abort()`が呼ばれたりしても、ダイアログは出しません。assertの内容(式・ファイル・行番号)と、そのとき実行していたテストケース名(`[ABORT] <名前>`)を標準エラーへ出し、終了コード`3`ですぐに終わります。結果は全ケースの実行後にまとめて出力するため、この場合は標準出力には何も出ません(`--json`でもJSONは出ません)。終了コードが`0`/`1`以外なら中断とみなしてください。ダイアログを出してデバッガをアタッチしたいときは`--interactive`を付けます。

現在カバーしているのは、GPU/ウィンドウに依存しない純粋ロジックのみです:
- `Common/Id.h` — 世代付きID(インデックス/世代のエンコード、`new_generation`、`DEFINE_TYPED_ID`)
- `Utilities/FreeList.h` — 削除済みインデックスの再利用ロジック
- `Content/ContentLoader.cpp` — `game.bin`の読み込み(0エンティティ、256以上の値、壊れたファイル、途中で失敗したときの後始末)

`Graphics/Direct3D12/*` などGPUデバイス初期化が前提のコードは対象外です。そちらの動作確認は`EngineTest`を実行し、目視で確認してください。

### ヘッドレス実行 (`--game-bin`)

`EngineUnitTests.exe`に`--game-bin`を付けると、ユニットテストの代わりに、エディタが書き出す`game.bin`を読み込み、スクリプトの更新(`script::update`)を指定フレーム数だけ回して終了します(#11の第一段)。ウィンドウもGPUも使いません。`game.bin`の読み込みとスクリプトの更新は、ゲームのexe(`Core/EngineWin32.cpp`)と同じ`content::load_game`/`script::update`を使い、実行ループと引数の解析は`DXForgeEngine/Core/Headless.cpp`にあります。

```powershell
./x64/Debug/EngineUnitTests.exe --game-bin path/to/game.bin --frames 10 [--dt 0.016] [--json]
./x64/Debug/EngineUnitTests.exe --help   # 引数と終了コードの一覧
```

- `--frames <N>`(必須): `script::update`を呼ぶ回数(0以上)。`--dt <秒>`: 毎回渡すデルタ時間(既定0.016)。
- `--json`: 結果を1行のJSONで標準出力に出します。ログは標準エラーに出ます。引数の誤りもJSONで返します。
  ```json
  {"mode":"headless","game_bin":"...","frames_requested":10,"frames_run":10,"dt":0.016,"success":true,"exit_code":0,"error":null,
   "entity_count":1,"entities":[{"id":0,"position":[10,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}]}
  ```
  失敗したときは`"error":{"code":"script_not_found","message":"entity 1: script 'Foo' is not registered"}`のようになります。`entities`は最後のフレームの、生きているエンティティの状態です。

| 終了コード | 意味 |
|---|---|
| `0` | 成功(エンティティが0個の`game.bin`は空のシーンとして成功) |
| `2` | 引数の誤り |
| `3` | `abort()`(assertなど。ユニットテストと同じく、ダイアログは出さない) |
| `10` | `game.bin`が無い |
| `11` | `game.bin`が読めない(空のファイル、アクセスの拒否など存在の確認自体に失敗した場合を含む) |
| `12` | `game.bin`の形式が壊れている(途中で切れている・余りのバイトがある・値が範囲外) |
| `13` | 未知のコンポーネントの種類 |
| `14` | スクリプトが登録されていない |
| `15` | エンティティを作成できなかった |

制約: 描画はしません(Renderer/D3D12は初期化しない)。スクリプトはゲームのexeと同じく`REGISTER_SCRIPT`で実行ファイルに静的に登録されたものだけが使え、今は`EngineUnitTests`に入っているテスト用のスクリプト(`EngineUnitTests/HeadlessTestScripts.cpp`の`headless_test_mover`)だけです。ゲームのプロジェクト(`.dxproj`)の読み込み、スクリプトでの操作、スクリーンショットはまだありません。

ヘッドレス実行の回帰テストは、`game.bin`を`%TEMP%`に作って実行ファイルを起動し、終了コードと`--json`を確かめます(`build.ps1 -RunTests`とCIからも実行されます)。

```powershell
python EngineUnitTests/Tests/headless_run_test.py                  # x64/Debug/EngineUnitTests.exe
python EngineUnitTests/Tests/headless_run_test.py --config Release
```

エディタの基本操作(プロジェクトの作成 → スクリプトの追加 → ゲームコードDLLの読み込み → ゲームの実行)の確認手順は [docs/ManualVerification.md](docs/ManualVerification.md) にまとめています。

### 新しいテストケースの追加方法

1. `EngineUnitTests/` に `Tests_<対象>.cpp` を作成
2. `EngineUnitTests.vcxproj` と `.vcxproj.filters` の `ClCompile` に追加
3. 以下の形式でテストケースを書く

```cpp
#include "MicroTest.h"
#include "CommonHeaders.h"

DXFORGE_TEST_CASE(my_new_test)
{
	CHECK(1 + 1 == 2);   // 失敗しても後続のCHECKは実行される
	REQUIRE(true);       // 失敗したらこのテストケースのみ即中断する
}
```

**新規ファイルは必ずUTF-8 BOM付きで保存してください。** このリポジトリの既存ファイルは全て日本語のヘッダーコメントで始まっており、BOMが無いとMSVCがShift-JISと誤認してコンパイルエラー(一見無関係な箇所でのエラー)になります。

## 品質ゲート

### CI (GitHub Actions)

`.github/workflows/ci.yml` が、`main`/`develop`へのpushとすべてのPRで以下を実行します。

- **ネイティブビルド + 自動テスト** — `Debug`と`Release`の両構成(`Release`では`NDEBUG`で`assert`が無効化され挙動が変わるため、両方を検証)。警告をエラーとして扱います。
- **エディタビルド** — `dotnet build`で`DXForgeEditor`をビルド。

CIの対象範囲は意図的に絞っています:

- `ContentTools`はFBX SDKが必要なためランナーではビルドできません(ソリューション構成`Debug`/`Release`ではビルド対象外なので問題になりません)。
  ローカルでは既定でFBX SDK 2020.3.7の標準インストール先を参照します。別の場所にある場合は、環境変数または`/p:FbxSdkDir=<SDKのルート>`で指定してください(`ContentTools/FbxSdk.props`)。SDKが見つからない場合はビルド開始時に分かりやすいエラーで停止します。
- `EngineTest`はビルドのみ(実行にはウィンドウとGPUが必要)。
- `EngineUnitTests`はGPU・ウィンドウ不要なので、CIで実際に実行されます。ヘッドレス実行(`--game-bin`)のテスト(`EngineUnitTests/Tests/headless_run_test.py`)も`Debug`/`Release`の両方で実行されます。

### ビルド出力の要約

ビルドツールの出力は非常に大きくなり得るため(実測: ネイティブ約7,400文字、エディタはnull許容参照型の警告が残っていた頃に**約132,000文字**)、`build.ps1`は既定で要約だけを表示します。エラーは原因特定に必要なので全文表示し、警告はコード別の件数に畳みます。

エディタ側の警告はIssue #9で0件になり、ネイティブ側と同様に`-WarningsAsErrors`で警告をエラー化できます(エディタには`-p:TreatWarningsAsErrors=true`を付けてフルビルドします)。CIはこのオプション付きでビルドするため、新しい警告を入れるとCIが失敗します。

ビルド失敗の調査などで全文が必要な場合は`-DetailedLog`を指定してください。

### 警告のエラー化

ローカル開発では警告はエラーになりません。CIだけが厳格に扱います。

```powershell
./build.ps1 -Target native -WarningsAsErrors   # ローカルでCIと同じ厳しさで確認したいとき
```

**注意:** MSBuildの`/p:TreatWarningAsError=true`はグローバルプロパティであり、**C++プロジェクトには効きません**(`/WX-`のままになります)。そのためルートの`Directory.Build.props`で`ItemDefinitionGroup`として設定し、`/p:DXForgeWarningsAsErrors=true`で切り替える方式にしています。`build.ps1 -WarningsAsErrors`がこれを渡します。

現在、ネイティブ側は`Debug`/`Release`ともに警告0件です。この状態を維持してください。

**日本語をナロー文字列リテラルに書かないでください。** CIランナーのシステムコードページは1252(日本語環境ではない)で、MSVCはナロー文字列を実行文字セットへ変換するため、日本語は1文字ごとに`C4566`が出てビルドが失敗します。これはCI特有のノイズではなく、ビルドするマシンのロケールによって文字列データが変わってしまう実際の移植性問題です。コメントは問題ありません(リテラルのみが対象)。日本語が必要な場合はワイド文字版API(`OutputDebugStringW(L"...")`)を使ってください。なお`#if _DEBUG`の中にある場合、Debug構成だけが失敗するため原因が分かりにくくなります。

### コーディング規約の自動化

`.editorconfig`に、実測に基づいた文字コードとインデントの規約を定義しています。とくに**C/C++のUTF-8 BOM必須**(`charset = utf-8-bom`)は、BOM無しだとMSVCがShift-JISと誤認してコンパイルエラーになる問題を、エディタ側で自動的に防ぐためのものです。

### Codexによるレビュー (`CodexReview.ps1`)

PRの差分を[Codex CLI](https://github.com/openai/codex)(`codex review`)にレビューさせ、実装したエージェントとは別の視点から指摘を得ます。PRのheadを`%TEMP%`の下の使い捨てのworktreeに展開してレビューし、終わったら削除します。ビルドやテストは行わないので、CIの代わりにはなりません。1PRあたり1〜2分かかります。

```powershell
# 結果を表示するだけ
./CodexReview.ps1 -Pr 52

# 結果をPRにコメントとして投稿する
./CodexReview.ps1 -Pr 52 -Post
```

Codex CLIがインストール済みで、ログインしている必要があります。Windowsでは、Codexのサンドボックスが無効だと、レビューのためのコマンドがすべて拒否されます。そのため、スクリプトは呼び出しごとに`-c windows.sandbox="unelevated"`を渡しています。

## 既知の制約

- `EngineTest`は対話的なビジュアルテストで、ウィンドウとGPUが必須のため、CLI/CIでは実行できません。
- グラフィックスバックエンドはDirect3D12のみ実装済みです。Vulkan/OpenGLは未実装のスタブです。
- **シェーダファイルの文字コードが統一されていません。** 日本語コメントを含む`.hlsl`/`.hlsli`の8ファイルがShift-JISのまま残っており(C++側だけが過去にUTF-8へ変換された)、残りはASCIIです。DXCは現状これを許容していますが、統一するのが望ましい状態です。`.editorconfig`でもこのためHLSLの`charset`は指定していません(指定すると保存時に文字化けする恐れがあるため)。
- `.clang-format`は未導入です(既存コード全体が再フォーマットされ巨大な差分になるため見送り)。

## 今後の方向性

将来的に、Unity CLI(`-batchmode`等)のように、エディタUIを介さずCLIからエンジンを操作・ビルドできる「AIフレンドリー」な構成を目指しています。`EngineUnitTests`はその最初の一歩(ウィンドウ/GPU無しでエンジンコードを動かすヘッドレスなCLIプログラム)という位置づけです。`--game-bin`による`game.bin`のヘッドレス実行(上の「ヘッドレス実行」)がその第一段で、今後、`--load-project`のような引数、スクリプトでの操作、描画とスクリーンショット、ContentToolsのCLI化(#10)との接続などを通じて拡張していく想定です。

## サードパーティライセンス

`Licenses/` 配下にサードパーティコンポーネントのライセンスをまとめています(例: `Licenses/MikkTSpace/LICENSE.txt`)。
