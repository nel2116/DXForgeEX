# DXForgeEX

[Game Engine Series (TheGameEngineers / Arash Khatami)](https://www.youtube.com/playlist?list=PLU2nPsAdxKWQYxkmQ3TdbLsyc1l2j25XM) と、その実装である [Primal](https://github.com/TheGameEngineers/Primal) を参考に実装している、学習用の3Dゲームエンジン + WPFレベルエディタです。

C++によるプラットフォーム非依存のエンジンコア、C ABIのDLL境界、その上で動くC#/WPFエディタ、オフラインのコンテンツ(FBX/テクスチャ)インポートパイプラインという構成を踏襲しています。現在の実装は、PBRの直接光(GGX/Smith/Schlickフレネル)まで到達しており、IBL(Image Based Lighting)は未実装です。

## 必要環境

- Windows 10/11
- Visual Studio 2022 (ワークロード: 「C++によるデスクトップ開発」+「.NETデスクトップ開発」、`v143` toolset)
- .NET 6 SDK (`dotnet` CLI。`DXForgeEditor`のビルドに使用)
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
| `DXForgeEditor` | WPFアプリ (.NET 6) | レベルエディタ |

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
| `-RunTests` | スイッチ。ビルド後に`EngineUnitTests.exe`を実行 | - |
| `-WarningsAsErrors` | スイッチ。ネイティブ側の警告をエラーとして扱う(CI用) | - |
| `-Json` | スイッチ。1行のJSON要約を標準出力に出す | - |

**注意:** ネイティブ側は必ず`DXForge.sln`経由でビルドしてください。`.vcxproj`を直接MSBuildに渡すと、`$(SolutionDir)`がプロジェクト自身のディレクトリにフォールバックしてしまい、インクルードパスが崩れてビルドに失敗します(MSBuildの既知の挙動)。`build.ps1`は常に`/t:<プロジェクト名>`で`DXForge.sln`を経由するため、この問題を踏みません。

## テスト

`EngineUnitTests` はGPU・ウィンドウを使わない自動テストです。ビルド後、生成された実行ファイルを直接実行できます。

```powershell
./x64/Debug/EngineUnitTests.exe          # 人間が読める出力
./x64/Debug/EngineUnitTests.exe --json   # 機械可読なJSON出力
```

終了コードは全テストPASSなら`0`、1件以上FAILなら`1`です。各失敗には式・ファイル・行番号が出力されます。

現在カバーしているのは、GPU/ウィンドウに依存しない純粋ロジックのみです:
- `Common/Id.h` — 世代付きID(インデックス/世代のエンコード、`new_generation`、`DEFINE_TYPED_ID`)
- `Utilities/FreeList.h` — 削除済みインデックスの再利用ロジック

`Graphics/Direct3D12/*` などGPUデバイス初期化が前提のコードは対象外です。そちらの動作確認は`EngineTest`を実行し、目視で確認してください。

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

- `ContentTools`はFBX SDKを絶対パスで参照しているためランナーではビルドできません(ソリューション構成`Debug`/`Release`ではビルド対象外なので問題になりません)。
- `EngineTest`はビルドのみ(実行にはウィンドウとGPUが必要)。
- `EngineUnitTests`はGPU・ウィンドウ不要なので、CIで実際に実行されます。

### 警告のエラー化

ローカル開発では警告はエラーになりません。CIだけが厳格に扱います。

```powershell
./build.ps1 -Target native -WarningsAsErrors   # ローカルでCIと同じ厳しさで確認したいとき
```

**注意:** MSBuildの`/p:TreatWarningAsError=true`はグローバルプロパティであり、**C++プロジェクトには効きません**(`/WX-`のままになります)。そのためルートの`Directory.Build.props`で`ItemDefinitionGroup`として設定し、`/p:DXForgeWarningsAsErrors=true`で切り替える方式にしています。`build.ps1 -WarningsAsErrors`がこれを渡します。

現在、ネイティブ側は`Debug`/`Release`ともに警告0件です。この状態を維持してください。

### コーディング規約の自動化

`.editorconfig`に、実測に基づいた文字コードとインデントの規約を定義しています。とくに**C/C++のUTF-8 BOM必須**(`charset = utf-8-bom`)は、BOM無しだとMSVCがShift-JISと誤認してコンパイルエラーになる問題を、エディタ側で自動的に防ぐためのものです。

## 既知の制約

- `EngineTest`は対話的なビジュアルテストで、ウィンドウとGPUが必須のため、CLI/CIでは実行できません。
- グラフィックスバックエンドはDirect3D12のみ実装済みです。Vulkan/OpenGLは未実装のスタブです。
- **シェーダファイルの文字コードが統一されていません。** 日本語コメントを含む`.hlsl`/`.hlsli`の8ファイルがShift-JISのまま残っており(C++側だけが過去にUTF-8へ変換された)、残りはASCIIです。DXCは現状これを許容していますが、統一するのが望ましい状態です。`.editorconfig`でもこのためHLSLの`charset`は指定していません(指定すると保存時に文字化けする恐れがあるため)。
- `.clang-format`は未導入です(既存コード全体が再フォーマットされ巨大な差分になるため見送り)。

## 今後の方向性

将来的に、Unity CLI(`-batchmode`等)のように、エディタUIを介さずCLIからエンジンを操作・ビルドできる「AIフレンドリー」な構成を目指しています。`EngineUnitTests`はその最初の一歩(ウィンドウ/GPU無しでエンジンコードを動かすヘッドレスなCLIプログラム)という位置づけです。今後、`--load-project`のような引数や、ContentToolsのCLI化などを通じて拡張していく想定です。

## サードパーティライセンス

`Licenses/` 配下にサードパーティコンポーネントのライセンスをまとめています(例: `Licenses/MikkTSpace/LICENSE.txt`)。
