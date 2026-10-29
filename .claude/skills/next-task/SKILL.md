---
name: next-task
description: DXForgeEX で次に取り組むべき作業を、リポジトリの状態(develop の CI、開いている PR と Issue、Primal の進み具合、確認待ちの項目)から調べて提案する。ユーザーに「次に行うべきタスクは?」「他にやるべきことは?」「次は何をする?」と聞かれたときに使う。提案するだけで、作業には着手しない。
---

# 次の作業を提案する

ユーザーはこの質問のあと、たいてい「そのためのプロンプトを提示して」と続ける。だから、**提案だけをして作業は始めない**。選ばれた作業の指示文は `orchestration-prompt` スキルで作る。

## 1. 状態を集める

```bash
git fetch -q origin
gh run list --branch develop --workflow ci.yml --limit 1 --json conclusion,headSha,displayTitle
gh pr list --state open --json number,title,labels,headRefName
gh issue list --state open --limit 50 --json number,title,labels
git worktree list
```

- 開いている PR は、なぜ開いたままかを本文とコメントから確かめる(GUI 確認待ち、`hold` ラベル、Codex の指摘待ちなど)。
- 確認待ちの項目は、メモリと、PR 本文の「マージ後に確かめる項目」「GUI の確認手順」から拾う。
- Primal の進み具合は、Issue のタイトルの `Primal Day NNN` と、`docs/dev/Architecture.md` の「Renderer status」から判断する。次の Day が Issue になっていなければ、それも候補にする(Primal: https://github.com/TheGameEngineers/Primal のコミット履歴の順)。

## 2. 優先順位を付ける

上から順に優先する。

1. **develop の CI が失敗している** → 何よりも先に直す。
2. **終わりかけの作業**: GUI 確認だけが残った PR、Codex の指摘への対応が残った PR。新しい作業より先に閉じる。
3. **開発の流れを止めている不具合**: ビルド、テスト、レビュー、マージ、後片付けが失敗するもの。今後のすべての作業に効く。
4. **Primal の次の Day**(機能の前進)。
5. **AI/CLI で扱いやすいエンジンにする作業**(#11 の続きなど、無人で確かめられる範囲を広げるもの)。
6. そのほかの改善。

さらに、次の観点も添える。

- **GUI 確認の要否**: ユーザーが不在なら、ヘッドレスで完了まで確かめられるものを優先する。
- **並行できるか**: 編集するファイルが重ならない作業どうしなら、worker を分けて並行にできる。
- **大きさ**: 1 つの PR に収まるか。収まらないなら分け方も示す。

## 3. 提示する

日本語で、次の形で短く示す。

- 状態の要約(2〜3 行): develop の CI、開いている PR、確認待ちの項目。
- 候補の表(3〜5 件): 作業(Issue 番号)、理由、大きさ、GUI 確認の要否、並行できる相手。
- **おすすめ**: 1 つ(並行なら組み合わせ)と、その理由。
- 最後に「この作業のプロンプトを作りますか?」と聞いて止まる。
