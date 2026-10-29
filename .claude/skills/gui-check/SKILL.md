---
name: gui-check
description: DXForgeEX のエディタやゲームを起動し、GUI で動作を確かめて結果を PR に残す。ユーザーに「GUI 確認を行って」「エディタを起動して確認して」と言われたとき、PR に書かれた GUI の確認手順を実行するときに使う。ユーザーの指示がないときは行わない(コーディネーターも worker も)。
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
  - 探す: `EnumWindows`。撮る: `CopyFromScreen`。クリック: `SetCursorPos` と `mouse_event`。入力: `SendKeys`。閉じる: `WM_CLOSE`。
  - どれも per-monitor DPI aware にして、物理座標を使う。
  - スクリプトは scratchpad に書き、パスを指定して実行する。
- ドロップダウンのメニュー(Add Component)は、実際のマウス操作でないと開かない。
- エクスプローラーからのドラッグ&ドロップも、`SendInput` で実際にドラッグすればできる。
- F5(ゲームの実行)は orca の `press-key` では送れないので、`SendKeys` で送る。
- スクリーンショットが真っ黒になったり別のウィンドウが写ったりするのは、エディタがほかのウィンドウの後ろにあるため。`SetForegroundWindow` の前に、ALT キーを `keybd_event` で押して離すと前面に出せる。
- ゲームのデバッグ出力(`[DXForge] ...`)は、DTE で起動した VS の出力ペインに出る。出力ペインをクリックし、Ctrl+A、Ctrl+C でコピーして読む。読んだあと、クリップボードを元に戻す。

## 3. 結果を残す

- 確認手順の項目ごとに、期待どおりか、実際に何が見えたかを記録する。数値で確かめられるもの(ログの行、ファイルの有無、tlog のパス)は数値でも残す。
- **スクリーンショットは PR で画像として見えるようにする**(gh CLI では PR に画像を添付できない)。
  - origin の orphan ブランチ `pr-assets`(全 PR で共有、消さない)の `issue-NN/` に PNG を push する。作業中のブランチや worktree には触れずに行う(`git hash-object`/`commit-tree`、または別の worktree を使う)。`pr-assets` への push では、CI も pr-automation も動かない。
  - PR 本文かコメントに、コミットの SHA で固定した URL で埋め込む: `https://github.com/nel2116/DXForgeEX/blob/<sha>/issue-NN/<file>.png?raw=true`。URL が 200 を返すことを確かめる。
  - ローカルにもコピーを残す。
- 確認の結果は PR にコメントする。問題が見つかったら、`create-issue` スキルで Issue にする(1 問題 1 件)。

## 4. 片付け

- 確認が終わったら、ゲーム、DTE で開いた VS、エディタを `WM_CLOSE` で閉じる。worktree の DLL を掴んだままだと、`CleanupMerged.ps1` が worktree を消せない。
- Debug ビルドの assert ダイアログで止まったプロセスが残っていないかを確かめる。
- 確認用プロジェクト(`GuiCheck<N>`)は、ユーザーに聞いてから消す。
