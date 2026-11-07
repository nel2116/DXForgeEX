---
name: gui-check
description: DXForgeEX のエディタやゲームを GUI で動かして確かめ、結果とスクリーンショットを PR の本文に載せる。PR に書かれた GUI の確認手順を実行するとき、ユーザーに「GUI 確認を行って」「エディタを起動して確認して」と言われたときに使う。コーディネーターは自分で操作せず、start-gui-check.ps1 で別のワーカーに任せる。
---

# GUI で確認する

<!-- last-reviewed: 2026-11-07 -->

エディタやゲームの動作、描画の結果は自動テストで確かめられないので、実際に画面を操作して確かめる。

## コーディネーターは `start-gui-check.ps1` を使う

```powershell
./.claude/skills/gui-check/start-gui-check.ps1 -Pr <N>           # 起動(-DryRun で仕様を見る、-Size normal で大きいモデル)
./.claude/skills/gui-check/start-gui-check.ps1 -Pr <N> -Cleanup  # worker_done と worker-release のあとで worktree を消す
```

PR の本文の `## GUI の確認手順`(番号付き)を読み、`worker-spec.md` と連結した仕様で新しいワーカーを起動する。ワーカーは PR の head をビルドして確かめ、画像を PR の本文に載せ、手順ごとの 1 行の結果だけを `worker_done` で返す(件名は `GUI 確認 PR #N: PASS|NG|未完了`)。マージの前に、`worker_done` の `head:` が今の PR の head と同じことを確かめる。違えば(確認のあとに push された)確認をやり直す。コーディネーターは自分で操作せず、画像も読まない(フックが止める)。マウスとキーの操作はデスクトップ全体に作用するので、GUI 確認は同時に 1 つだけにする(`gui-check-pr-*` の worktree が残っているか、別の起動の途中なら、終了コード 4 で起動しない)。

## ワーカーは `gui.ps1` を使う

要素は操作のたびに UI Automation で読み直すので、番号は古くならない。子ウィンドウ(ダイアログ)も読める。`orca computer` は使わない。詳しい引数はスクリプトの先頭にある。

```powershell
$gui = "./.claude/skills/gui-check/gui.ps1"
$out = & $gui -Action create -Project GuiCheck200                # 最後の行が PID <n>
$p = [int](($out | Select-Object -Last 1) -replace 'PID ', '')
& $gui -Action close -ProcId $p

# GUI なしで中身を作る(エディタを閉じてから。開いていると dxforge は 44 で断る)
& $gui -Action prepare -Project GuiCheck200 -Import F:\DXForgeTestAssets\LODTest.fbx
& $gui -Action prepare -Project GuiCheck200 -Op '{"op":"create-entity","name":"Box"}'   # -Ops <file> でまとめて

$out = & $gui -Action open -Project GuiCheck200                  # 基準の版は -Exe <exe>
$p = [int](($out | Select-Object -Last 1) -replace 'PID ', '')
& $gui -Action tree -ProcId $p -Pattern 'ListItem|^Text'         # 判定はまずテキストで
& $gui -Action click -ProcId $p -Target AddEntity                # 名前の一覧は -Action names
& $gui -Action click -ProcId $p -Target 'el:Text:^Box$'          # 要素で指定。Text は左端を押す
& $gui -Action drag -ProcId $p -Target 'el:Text:^lodtest$' -To EntityDrop
& $gui -Action keys -ProcId $p -Keys '{F5}'
& $gui -Action shot -ProcId $p -Region Hierarchy -OutFile $env:TEMP\gui-check-pr-200\pr-hierarchy.png
& $gui -Action switch -ProcId $p -Project GuiCheck200b           # File > Open Project。作るなら -Action create -ProcId $p
& $gui -Action close -ProcId $p                                  # Project Browser が残っても閉じ、終了を確かめる
& $gui -Action end-session -ProcId $p                            # ログオフを対象のプロセスにだけ模擬し、終了コードを確かめる(実際にはログオフしない)
& $gui -Action remove-project -Project GuiCheck200               # GuiCheck で始まる名前だけ
& $gui -Action diff -Dir $env:TEMP\gui-check-pr-200              # base-*.png と pr-*.png の画素の差
```

部品が解決していること: タブや一覧は実際のマウスで押す。ALT による前面化は対象が前面でないときだけ行うので、開いたメニューは閉じない。Hierarchy の行は左端を押すので Remove を誤って押さない。空のリストは `EntityDrop` に落とす。

- 判定はテキストと数値を優先する。撮影は領域を絞る(`-Region` の名前か `x,y,w,h`、`-Target` の要素。幅は既定で 1000 まで)。座標はウィンドウの左上からの物理座標で、名前付きの座標は既定の大きさのウィンドウのもので、表示倍率に合わせて変換する。
- ダイアログは `-Title <タイトルの一部>` で指定する。`window.ps1 -Action list -ProcId <PID>` で一覧を見られる。
- デバッグ出力(`[DXForge] ...`、D3D12 のデバッグレイヤー)は `capture-debug-output.ps1` で取り込む(使い方はスクリプトの先頭)。

## 準備

- PR の worktree をビルドする: `./build.ps1 -Configuration DebugEditor -Target all` と `./build.ps1 -Configuration Debug -Target native -Project DXForgeEngine`(ゲームは Debug でリンクする)。
- エディタは、自分の実行ファイルがあるリポジトリのエンジンでゲームをビルドする(#117)。ユーザー環境変数 `DXFORGE_ENGINE` は切り替えない。
- 確認用のプロジェクトは `GuiCheck<N>` の名前で新しく作り、既存のものは使わない。場所はユーザー環境変数 `DXFORGE_PROJECTS`、なければ `%USERPROFILE%\DXForgeProjects\`(#141)。
- ソースファイルがないと、ゲームの exe も DLL も作られない(#118)。New Script でスクリプトを足す。スクリプトが動いたかは、`begin_play`/`update` で目印のファイルを書かせて判定する。
- EngineTest は `..\..\x64\*.model` と `*.texture`(git の管理外)を読むので、`x64\Debug` を作業フォルダにして起動する。新しい worktree では、ルートのチェックアウトの `x64` からコピーし、終わったら消す。

## 結果を残す

**画面で確かめたときは、スクリーンショットを必ず PR の本文に載せる。** コメントだけにはしない。PR がない確認は、関係する Issue のコメントに載せる。

- 変更の前後を `base-<名前>.png` と `pr-<名前>.png` の組で 1 つのフォルダ(`%TEMP%\gui-check-pr-<N>\`)に置く。基準の版は develop(ルートのチェックアウトの実行ファイルでよい。ブランチは切り替えない)。
- `./.claude/skills/gui-check/push-pr-assets.ps1 -Pr <N> -Dir <フォルダ> -Summary <結果の Markdown> -BaseLabel "develop(<sha>)"` で載せる。`pr-assets` ブランチの `pr-<N>/gui-check/` に push し、本文の `<!-- gui-check:start -->` の節を置き換える(やり直しても節は 1 つ)。`-DryRun` で先に見られる。
- `-Summary` には、確認の日付、比べた版(sha)、手順ごとの結果(期待どおりか、数値)を書く。

## 片付け

起動したゲーム、VS、エディタを閉じる(worktree の DLL を掴んだままだと消せない)。assert のダイアログで止まったプロセスが残っていないかを確かめる。確認用のプロジェクトは `-Action remove-project` で消す。
