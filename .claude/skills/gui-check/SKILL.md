---
name: gui-check
description: DXForgeEX のエディタやゲームを起動し、GUI で動作を確かめて、結果とスクリーンショット(変更の前後)を PR の本文に載せる。ユーザーに「GUI 確認を行って」「エディタを起動して確認して」と言われたとき、PR に書かれた GUI の確認手順を実行するときに使う。ユーザーの指示がないときは行わない(コーディネーターも worker も)。
---

# GUI で確認する

エディタやゲームの動作、描画の結果は自動テストで確かめられないので、Claude が実際に画面を操作して確かめる。2026/10/27〜28 に PR #108・#114・#121 の確認をこの方法で行った。

## 1. 準備

- **どの worktree のエディタで確かめるか**を決める。PR を確かめるなら、その PR の worktree をビルドする。
  - `./build.ps1 -Configuration DebugEditor -Target all`(新しい worktree でも 1 回で通る、#116 で修正済み)
  - ゲームは Debug でリンクするので、`./build.ps1 -Configuration Debug -Target native -Project DXForgeEngine` も先に実行しておく。
- エディタは、自分の実行ファイルがあるリポジトリ(`x64\<構成>\` の 2 つ上)のエンジンで、ゲームをビルドする(#117)。ユーザー環境変数 `DXFORGE_ENGINE` は切り替えない。食い違うときは LoggerView に Warning が出る。リンクされた lib は、ゲームの `GameCode\x64\Debug\*.tlog` で確かめられる。
- **確認用のプロジェクトは新しく作る。** 既存のプロジェクトは使わない。Empty Project から `GuiCheck<N>` を作る(場所はエディタの既定の DXForgeProjects フォルダ)。
  - ソースファイルがないと、ゲームの exe も DLL も作られない(「ゲームコードDLLファイルのロードに失敗」になる、#118)。まず New Script でスクリプトを足す。
  - スクリプトが動いたかは、`begin_play`/`update` で、`GetModuleFileNameA` の結果を含む目印のファイルを書かせて判定する。パスはスラッシュ区切りにし、Write/Edit ツールで書く(heredoc だと `\` が消える)。

## 2. 操作

- `orca computer`(`computer-use` スキル)で見えるのはメインウィンドウだけ。子ウィンドウ(Texture Editor、Geometry Editor、Configure Import Settings、New Script、ゲームの「DXForge Game」、assert のダイアログ)は見えない。子ウィンドウは、PowerShell から Win32 API を呼ぶ自前のスクリプトで操作する。
  - 列挙・撮影・終了は `window.ps1` を使う(`-Action list|shot|close -ProcId <PID> [-Title <タイトルの一部>] [-OutFile <png>]`)。撮る前に前面に出すので、真っ黒の画像にならない。
  - クリックは `SetCursorPos` と `mouse_event`、入力は `SendKeys`。per-monitor DPI aware にして、物理座標を使う。
  - そのほかのスクリプトは scratchpad に書き、パスを指定して実行する。
- ドロップダウンのメニュー(Add Component)は、実際のマウス操作でないと開かない。
- エクスプローラーからのドラッグ&ドロップも、`SendInput` で実際にドラッグすればできる。
- F5(ゲームの実行)は orca の `press-key` では送れないので、`SendKeys` で送る。
- スクリーンショットが真っ黒になったり別のウィンドウが写ったりするのは、エディタがほかのウィンドウの後ろにあるため。`SetForegroundWindow` の前に、ALT キーを `keybd_event` で押して離すと前面に出せる。
- デバッグ出力(`[DXForge] ...`、D3D12 のデバッグレイヤーのメッセージ、`ReportLiveDeviceObjects` の警告)は、`capture-debug-output.ps1` で、VS を起動せずにファイルに取り込める(`powershell.exe` で別のプロセスとして起動する。使い方はスクリプトの先頭)。各行に `[PID]` が付くので、ほかのプロセスの出力と分けられる。デバッガをつないだプロセスの出力は取り込めない。
  - DTE で起動した VS の出力ペインを読む方法もある。出力ペインをクリックし、Ctrl+A、Ctrl+C でコピーして読む。読んだあと、クリップボードを元に戻す。
- EngineTest は `..\..\x64\*.model` と `*.texture`(git の管理外)を作業フォルダからの相対パスで読むので、`x64\Debug` を作業フォルダにして起動する。新しい worktree では、ルートのチェックアウトの `x64` からモデルとテクスチャをコピーし、終わったら消す。

## 3. 結果を残す

**画面で確かめたときは、スクリーンショットを必ず PR の本文に載せる。** ユーザーに言われなくても行う。コメントだけにはしない(コメントは流れて、マージの判断のときに見落とされる)。PR がない確認(develop の動作の確認など)は、関係する Issue のコメントに載せる。

- 確認手順の項目ごとに、期待どおりか、実際に何が見えたかを記録する。数値で確かめられるもの(ログの行、ファイルの有無、tlog のパス、画素の差)は数値でも残す。
- **変更の前後を撮る。** 描画や画面の見た目が変わっていないことを確かめるときは、基準の版(develop、ルートのチェックアウトでビルドしてよい。ブランチは切り替えない)と PR の版を同じ手順で撮り、`base-<名前>.png` と `pr-<名前>.png` の名前で 1 つのフォルダ(`%TEMP%` の下)に置く。見た目が変わる PR では、変わったことが分かるように同じく前後を撮る。
- 撮ったら `push-pr-assets.ps1` で PR に載せる。
  ```powershell
  ./.claude/skills/gui-check/push-pr-assets.ps1 -Pr <N> -Dir <PNG のフォルダ> -Summary <結果の Markdown> -BaseLabel "develop(<sha>)"
  ```
  - origin の orphan ブランチ `pr-assets`(全 PR で共有、消さない)の `pr-<N>/gui-check/` に PNG を push する。作業中のブランチや worktree には触らない。`pr-assets` への push では、CI も pr-automation も動かない。
  - PR の本文の `<!-- gui-check:start -->`〜`<!-- gui-check:end -->` の節を、`-Summary` の本文と画像(`base-*`/`pr-*` の組は比較表、それ以外は 1 枚ずつ)で置き換える。節がなければ `Refs`/`Closes` の行の前に足す。やり直しても節は 1 つのまま。
  - 画像の URL はコミットの SHA で固定し、すべてが 200 を返すことを確かめてから本文を書き換える。`-DryRun` で、作る節を先に見られる。
  - `-Summary` には、確認の日付、比べた版(sha)、手順ごとの結果(期待どおりか、数値)を書く。
  - ローカルにもコピーを残す。
- 問題が見つかったら、`create-issue` スキルで Issue にする(1 問題 1 件)。その Issue にもスクリーンショットを載せる。

## 4. 片付け

- 確認が終わったら、ゲーム、DTE で開いた VS、エディタを `WM_CLOSE` で閉じる。worktree の DLL を掴んだままだと、`CleanupMerged.ps1` が worktree を消せない。
- Debug ビルドの assert ダイアログで止まったプロセスが残っていないかを確かめる。
- 確認用プロジェクト(`GuiCheck<N>`)は、ユーザーに聞いてから消す。
