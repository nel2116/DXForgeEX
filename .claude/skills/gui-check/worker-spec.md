<!-- last-reviewed: 2026-11-05 -->

## タスク: PR #{{PR}} の GUI 確認(#195)

PR: {{URL}}(「{{TITLE}}」)。確かめる head: `{{HEAD}}`(ブランチ `{{HEAD_REF}}`、向き先 `{{BASE_REF}}`)。

あなたは実装者とは別の、GUI 確認だけを行うワーカーです。実装者の考えを知らない目で、手順に書かれたことを確かめます。

### 共通のルールとの違い(こちらを優先する)

- GUI を使う。操作は `.claude/skills/gui-check/gui.ps1`(使い方は `gui-check` スキルの `SKILL.md`)で行う。`orca computer` は使わない。
- 最初に PR の head に切り替える: `git fetch origin refs/pull/{{PR}}/head` のあと、`git rev-parse FETCH_HEAD` が上の head と一致することを確かめて `git checkout --detach {{HEAD}}` にする。一致しなければ(PR が更新された)、確認せずに未完了として報告する。
- ブランチは付け直さない。コミット、push(`push-pr-assets.ps1` を除く)、PR の作成、`PrePr.ps1`、CI の待機、`CodexReview.ps1` は行わない。製品のコードを直さない。
- `ask` で許可を待たない。終わったら下の形式で `worker_done` を 1 回送って終わる。
- 手順どおりに進めない、または判断できないときだけ `ask` する。不具合を見つけても自分で直さず、Issue も立てず、NG として報告する。

### 進め方

1. ビルドする: `./build.ps1 -Configuration DebugEditor -Target all`、続けて `./build.ps1 -Configuration Debug -Target native -Project DXForgeEngine`。
2. 確認用のプロジェクトを `GuiCheck{{PR}}`(2 つ目からは `GuiCheck{{PR}}b` など)の名前で作る。中身はできるだけ `gui.ps1 -Action prepare` で作る。既存のプロジェクトは使わない。
3. 下の手順を番号の順に確かめる。判定はまず `-Action tree` のテキスト、ログ、ファイル、数値で行う。画像を `Read` するのは、見た目そのものが判定の対象のときだけにする。撮影は領域を絞る。変更前の画面が要る手順では、ルートのチェックアウト(`F:\HAL\Self\DXForgeEX`、ブランチは切り替えない)の `x64\DebugEditor\DXForgeEditor.exe` を `-Exe` で起動し、`base-<名前>.png` と `pr-<名前>.png` の組にする。
4. 画像は `%TEMP%\gui-check-pr-{{PR}}\` に置き、結果の Markdown を書いて、`./.claude/skills/gui-check/push-pr-assets.ps1 -Pr {{PR}} -Dir <フォルダ> -Summary <Markdown>` で PR の本文に載せる。Markdown には日付、確かめた head、手順ごとの結果を書く。
5. 片付ける: 起動したエディタ、ゲーム、VS をすべて閉じる(`-Action close`)。`-Action remove-project` で確認用のプロジェクトを消す。`%TEMP%` のフォルダを消す。

### 報告の形式

`worker_done` の `--subject` は `GUI 確認 PR #{{PR}}: PASS`(すべて期待どおり)、`NG`(期待と違う手順がある)、`未完了`(確かめられない手順がある)のどれか。`--outcome` は、確かめ終えたら NG でも `succeeded`、確かめ終えられなければ `failed`。`--body` は次の形で、これだけを書く(コーディネーターの文脈を増やさないため、画像の説明やログの全文は書かない)。共通のルールの「worker_done に書くこと」の代わりにこれを使う。

```text
head: <7 桁>
1. OK: <見えたことや数値を 1 行>
2. NG: <期待と実際を 1 行>
3. 未確認: <理由を 1 行>
本文: <PR の URL>(画像 <n> 枚)
片付け: エディタ終了、<消したプロジェクト>
```

### 手順(PR の本文から取り出したもの)

{{STEPS}}
