<!--
コーディネーター向け指示文の雛形。<...> を埋め、当てはまらない行は消す。
これまでに使った指示文(#10、#11/#107、#104、#116/#117)から共通部分を抜き出したもの。
-->
/orchestration スキルを使って、コーディネーターとして <Issue #N(一行の説明)> を <1 人 / N 人の worker に並行して> 進めさせてください。worker の監督、Codex レビューの指摘の転送、マージまでを担当してください。<ユーザーの在不在: 例「ユーザーは 22 時ごろまで不在なので、GUI は使わず、ヘッドレスで完了条件まで確かめられる範囲で進めます。」>

## 事前準備(コーディネーターが行う)
1. `git fetch` して origin/develop を最新にし、develop の CI が成功していることを確かめる。失敗していれば止めて報告する。
2. `./CleanupMerged.ps1` を実行し、ルートの develop を最新にする。警告が出たら内容を報告する(実際の問題として扱う)。<意図的に残している worktree があれば、その名前と理由: 例「PR #108 は GUI 確認待ちなので `issue-10-import-cli` は残っていて正しい」>
3. `orca skills get orchestration` で手順書を読み、`orca status --json` で Orca が動いていることを確かめる。新しい worktree を作るので `references/placement-and-remote.md` も読む。
4. `run-create --objective "<目的>"` で実行(Run)を 1 つ作る。
5. ルートのチェックアウト(develop)は切り替えない。`CodexReview.ps1` は、origin/develop から作ったコーディネーター用の detached worktree から実行し、終わったらその worktree を削除する。

## worker の起動
- `worker-start --spec "<下のタスク仕様>" --worktree new-top-level --agent claude --setup run` で起動する。worktree 名は `<issue-N-短い名前>`。
- 起動が agent_readiness でタイムアウトしたら、受け取った recovery の手順に従い、同じ worktree で `--retry-of` で起動し直す。新しい worktree は作らない。
- 全員を起動してから待つ(`check --wait --types "worker_done,escalation,question"`)。
- 質問(question)には仕様の範囲で答える。<方式の承認が要るなら: 「worker が方式を ask で提案してきたら、下の『方式の判断基準』に照らして承認するか、直してほしい点を返す」> 仕様の範囲を超える判断は、<ユーザーに確認してから答える / ユーザーが不在なら範囲を縮める方向で決め、理由を最後の報告に書く>。

## worker 共通のルール(タスク仕様に含める)
- リポジトリは DXForgeEX(GitHub の nel2116/DXForgeEX)。リポジトリ直下の `AGENTS.md`(Claude Code は `CLAUDE.md` 経由で読み込まれる)を最初に読み、それに従う。PR 本文と報告は日本語で書く。
- 自分の worktree は origin/develop から作った新しいトップレベルの worktree。それ以外の場所(特にルートのチェックアウト F:\HAL\Self\DXForgeEX)は編集せず、ブランチも切り替えない。PR の向き先は develop。マージはしない。
- ブランチ名は `<fix|feature>/issue-N-...`。`nel2116/` が付いていたら、最初に `git branch -m` で付け直す。
- コミットメッセージと PR タイトルは `[<fix|add>] <日本語の要約> (#N)`。PR 本文には「概要」「変更点」「動作確認」の節を設け、`Closes #N` を書く。
- C++ や C# を変更したら、push の前に `./build.ps1 -Target <native|editor> -WarningsAsErrors` と関係するテストを実行する。結果はコマンドと実際の出力で報告する。
- PR を作ったら `gh pr checks --watch` で CI を確認し、失敗していれば直す。
- <GUI を使わない場合: **GUI(`orca computer`/computer-use)は使わない。** GUI でしか確かめられない項目は、手順を PR に書くだけにする。>
- <GUI 確認がある場合: GUI の確認手順を PR に書く。確認そのものは、ユーザーの指示があるまで誰も行わない。スクリーンショットは `gui-check` スキルの方法(`pr-assets` ブランチ)で PR に貼る。>
- 自分で起動したプロセス(エディタ、ゲーム、ファイルをロックするもの)は、終わったら必ず終了させる。終了できないときは、PID と状態をコーディネーターに伝える。一時ファイルは %TEMP% の下に作り、終わったら削除する。
- PR を作り、CI が通り、ローカルでの確認も終わったら、`ask` で「PR 準備完了」(PR の URL 付き)を送り、Codex レビューの結果を待つ。**PR に付いた Codex のコメントを自分で読んで修正を始めない。** 指摘はコーディネーターが確かめてから渡す。受け取ったら、修正するか、修正しない理由を PR に返信する。
- **「worker_done を送ってよい」という返信が来るまで、ターンを終えずに待ち続ける。** `ask` がタイムアウトしたら、同じメッセージ ID で `ask --resume` して待ち直す。
- 使用量の上限に近づいたら、作業中の内容をコミットして push し、どこまで済んで何が残っているかをコーディネーターに伝えてから止まる。
- 作業中に別の不具合を見つけたら、自分では直さず、コーディネーターに報告する。
- worker_done には、PR の URL、確かめたこと(コマンドと実際の出力)、確かめられなかったこと、マージのあとに確かめる項目を書く。

## タスク仕様<A>(#N)

Issue #N を<直す/実装する>: https://github.com/nel2116/DXForgeEX/issues/N
(<Issue の一行の説明>)

`gh issue view N` で本文を読むこと。

### 前提(コーディネーターが確かめたこと)
- <コードを読んで確かめた事実を `ファイル:行` 付きで。推測には(推定)と付ける>
- <Primal の該当箇所があれば: Primal Day NNN / コミット>

### 内容
- Target: <編集してよいファイル・フォルダ>
- Change: <何をどう変えるか。推奨の方針と、違う判断をするなら実装の前に ask で確認すること>
- Constraints: <守ること。例: 警告 0 件のまま、ユーザー環境変数を書き換えない、`async void` の経路で例外を投げない、既存の書き方に合わせる>
- Ownership: <ほかの worker と衝突しないよう、触らない範囲>
- Observable acceptance:
  - <修正前の再現(できるなら)>
  - <修正後に確かめること。ヘッドレスで確かめられるものはテストにする>
  - <GUI でしか確かめられないもの(「GUI で確認」と明記)>

### 方式の判断基準(方式の承認を挟む場合だけ)
- <コーディネーターが承認するときの基準>
- <範囲外のこと>

### 完了条件
- <確かめられる条件を列挙。CI がすべて成功していること、PR に書くべきこと>

## Codex レビューとマージ(コーディネーターが行う)
1. 「PR 準備完了」が届き、CI が成功していることを確かめたら、`./CodexReview.ps1 -Pr <N> -Post` を実行する(この時点では `-SetStatus` を付けない)。
2. 指摘があれば、内容を確かめてから `send --to dispatch:<id>` でその worker に渡す。修正されたらレビューをやり直す。
3. 指摘がなくなったら:
   - <GUI 確認が要らない場合: `-SetStatus` を付けてレビューを実行し、自動マージさせる。>
   - <GUI 確認が要る場合: マージせずに止める。確認手順(PR に書かれたもの)をユーザーに示し、確認の結果を待つ。>
4. マージされたら `./CleanupMerged.ps1` を実行し、develop の CI を確かめる。<並行の場合: 片方が先にマージされて develop が進んだら、もう片方には develop をマージさせ、CI とレビューをやり直す。>

## 片付けと報告
- worker_done を受け取るたびに、内容を確かめてから `worker-release` する。`worker-list --terminal-state reclaimable` が空になるまでターンを終えない。
- 最後に、Issue ごとに、PR の番号、マージしたか、Codex の指摘とその対応、根拠(コマンドと出力)、未解決のこと、マージ後に確かめる項目を日本語で報告する。途中で見つかった新しい不具合は、`create-issue` スキルで Issue にする。
