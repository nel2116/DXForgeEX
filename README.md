# DXForgeEX

<!-- last-reviewed: 2026-11-08 -->

[Game Engine Series (TheGameEngineers / Arash Khatami)](https://www.youtube.com/playlist?list=PLU2nPsAdxKWQYxkmQ3TdbLsyc1l2j25XM) と、その実装である [Primal](https://github.com/TheGameEngineers/Primal) を参考に実装している、学習用の3Dゲームエンジン + WPFレベルエディタです。

C++によるプラットフォーム非依存のエンジンコア、C ABIのDLL境界、その上で動くC#/WPFエディタ、オフラインのコンテンツ(FBX/テクスチャ)インポートパイプラインという構成を踏襲しています。現在の実装は、PBRの直接光(GGX/Smith/Schlickフレネル)と、オフラインで事前計算したIBL(Image Based Lighting)まで到達しています。

この README は人が読むための概要です。ビルドやテストの詳細(`build.ps1` の引数、各CLIの引数・JSON・終了コードなど)は、エージェント向けに英語で `docs/` に置いています(下の「詳しい文書」)。

## 必要環境

- Windows 10/11
- Visual Studio 2022 (ワークロード: 「C++によるデスクトップ開発」+「.NETデスクトップ開発」、`v143` toolset)
- .NET 8 SDK以降 (`dotnet` CLI。`DXForgeEditor`のビルドに使用)
- DirectX 12対応GPU (レンダラーを実際に動かす場合のみ必要。`EngineUnitTests`はGPU/ウィンドウ不要)
- FBX SDK 2020.3.7 (`ContentTools`、つまりFBXと画像のインポートを動かす場合のみ必要)

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
| `DXForgeImport` | コンソールアプリ (.NET 8) | GUIなしでアセットをインポートするCLI。エディタと同じ`.asset`を書き出す(`*Editor`構成のみ) |
| `DXForgeCli` | コンソールアプリ (.NET 8) | `dxforge.exe`。GUIなしでプロジェクト(`.dxproj`)を読み込んで実行したり、シーンを操作したりするCLI(`*Editor`構成のみ) |

## ビルドとテスト

Visual Studioでは `DXForge.sln` を開き、任意の構成でビルドします。CLIでは PowerShell 7+ からルートの `build.ps1` を使います。必要なパッケージ(`packages/`)は初回のビルドで自動的に取得されます。

```powershell
./build.ps1                                                   # ネイティブ側+エディタ側 (DebugEditor)
./build.ps1 -Target native -Project EngineUnitTests -RunTests # 自動テストだけを手早く
./build.ps1 -Target editor                                    # エディタ側だけ
```

ネイティブ側は必ず`DXForge.sln`経由でビルドしてください(`.vcxproj`を直接ビルドするとインクルードパスが崩れます。`build.ps1`はこれを守っています)。

自動テストは、GPUとウィンドウを使わない`EngineUnitTests`と、エディタやCLIのコードを別のプロセスやリフレクションで確かめるスクリプトです。描画は、WARP(ソフトウェアのラスタライザ)で固定のシーンを描いて基準画像と比べます。それ以外の描画とエディタの操作は、実際に動かして目で確かめます(手順は [docs/ManualVerification.md](docs/ManualVerification.md))。

CI(GitHub Actions、`.github/workflows/ci.yml`)が、`main`/`develop`へのpushとすべてのPRで、`Debug`/`Release`のネイティブビルドと自動テスト、エディタのビルドとテスト、ファイル規約の検査(`Tests/check_conventions.py`)を、警告をエラーとして実行します。PRはさらに Codex のレビュー(`CodexReview.ps1`)を通してからマージします。

## エディタの設定

エディタは次のユーザー環境変数を読みます。

- `DXFORGE_PROJECTS`: Create ProjectのPath欄の既定値(例: `F:\DXForgeProjects`)。無ければ`%USERPROFILE%\DXForgeProjects\`。エディタは書き換えません。
- `DXFORGE_ENGINE`: ゲームのビルドに使うエンジンのパス。エディタの実行ファイルがあるリポジトリが優先され、それが見つからないときだけ使われます。これも使えないときは起動時にエンジンのフォルダを選ぶダイアログを出し、選んだパスをここに保存します。

## GUIなしで使うCLI

将来的に、Unity CLI(`-batchmode`等)のように、エディタUIを介さずCLIからエンジンを操作・ビルドできる「AIフレンドリー」な構成を目指しています。今あるのは次のものです。

- `DXForgeImport.exe`: FBXと画像をインポートし、エディタと同じ`.asset`を書き出します。
- `dxforge.exe --load-project`: `.dxproj`を読み込み、シーンを指定フレーム数だけ実行して、エンティティの状態をJSONで返します。`--ops`でシーンを読んだり変えたりできます。
- `EngineUnitTests.exe --game-bin` / `--render`: `game.bin`のヘッドレス実行と、テストシーンの画面外への描画(PNGの書き出し)。

いずれも `--help` で引数と終了コードの一覧を表示します。今後、スクリプトでの操作、プロジェクトの描画とスクリーンショットなどへ広げていく想定です。

## 詳しい文書

| 内容 | 文書 |
|---|---|
| エージェント向けの指示(Claude Code・Codex共通)と文書の地図 | [AGENTS.md](AGENTS.md) |
| 構成、レンダラーの状態、各プロジェクトの役割 | [docs/dev/Architecture.md](docs/dev/Architecture.md) |
| `build.ps1`の引数、パッケージの復元、警告、文字コード、テスト、CI | [docs/dev/BuildAndTest.md](docs/dev/BuildAndTest.md) |
| `DXForgeImport`の引数、設定、出力 | [docs/dev/DXForgeImport.md](docs/dev/DXForgeImport.md) |
| PRのレビューとマージ | [docs/dev/ReviewAndMerge.md](docs/dev/ReviewAndMerge.md) |
| 単位などの設計の決定 | [docs/design/](docs/design/) |

## 既知の制約

- `EngineTest`は対話的なビジュアルテストで、ウィンドウとGPUが必須のため、CLI/CIでは実行できません。
- グラフィックスバックエンドはDirect3D12のみ実装済みです。Vulkan/OpenGLは未実装のスタブです。
- `ContentTools`はFBX SDKが必要なため、CIではビルドもテストもしません(FBXと画像の実際のインポートはローカルで確かめます)。
- `.clang-format`は未導入です(既存コード全体が再フォーマットされ巨大な差分になるため見送り)。

## サードパーティライセンス

`Licenses/` 配下にサードパーティコンポーネントのライセンスをまとめています(例: `Licenses/MikkTSpace/LICENSE.txt`)。
