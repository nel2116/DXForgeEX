# Claude Code の PreToolUse フック(Bash / PowerShell): python3 の実行を止めて python を使わせる。
#
# このマシンの python3 は Windows ストアのスタブで、何も実行せずに黙って終わる。テストが「通った」ように
# 見えてしまうので、文章のルールではなくフックで止める。コマンドとして実行する位置の python3 だけを対象にし、
# 引用符・ヒアドキュメント・PowerShell のヒア文字列の中("git grep 'python3'" やコミットメッセージ)は止めない。
import json
import re
import sys

# シェルを完全に解析するのではなく、よくある書き方を止めるためのもの。コマンドの位置(行頭や ; & | ( の直後)に、
# 環境変数の代入(PYTHONUTF8=1 など)や env/time/exec/nohup/command を前置した python3 も含める。
PATTERN = re.compile(r"(?:^|[;&|(`\n]|\$\()\s*(?:(?:env|time|exec|nohup|command)\s+|\w+=\S*\s+)*"
                     r"python3(?:\.exe)?(?=\s|$|[;&|)`])")
# \$( や \` のようにエスケープされたものは実行されないので除く
SUBSTITUTION = re.compile(r"(?<!\\)\$\([^)]*\)|(?<!\\)`[^`\n]*`")
# 文字列の部分(順番に適用する)と、その中身が展開されるか(グループ "q" の引用符で決まる)。
# 展開されないものは丸ごと取り除き、展開されるものは $(...) とバッククォートのコマンド置換だけ残す
# (例: output="$(python3 x.py)" は実際に python3 を実行する)。
LITERALS = [
    # bash のヒアドキュメント: 区切り文字を ' でも " でも囲めば展開されない
    (re.compile(r"<<-?\s*(?P<q>['\"]?)(\w+)(?P=q)[^\n]*\n.*?^\s*\2\s*$", re.DOTALL | re.MULTILINE),
     lambda q: q == ""),
    # PowerShell のヒア文字列: @"..."@ は展開され、@'...'@ は展開されない
    (re.compile(r"@(?P<q>['\"])\r?\n.*?^(?P=q)@", re.DOTALL | re.MULTILINE), lambda q: q == "\""),
    # 引用符で囲んだ文字列: "..." は展開され、'...' は展開されない
    (re.compile(r"(?P<q>')[^']*'|(?P<dq>\")(?:\\.|[^\"\\])*\""), lambda q: q is None),
]


def runs_python3(command):
    for pattern, expands in LITERALS:
        def replace(match):
            if not expands(match.group("q")):
                return " "
            return " " + " ".join(SUBSTITUTION.findall(match.group(0))) + " "
        command = pattern.sub(replace, command)
    return PATTERN.search(command) is not None


def main():
    try:
        # Claude Code は UTF-8 で渡す。コンソールのコードページ(CP932)で読むとパスや本文が化けるのでバイトで読む
        payload = json.loads(sys.stdin.buffer.read())
    except ValueError:
        return 0
    command = (payload.get("tool_input") or {}).get("command") or ""
    if runs_python3(command):
        print("python3 はこのマシンでは Windows ストアのスタブで、何も実行されません。python を使ってください。",
              file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.stderr.reconfigure(encoding="utf-8")
    sys.exit(main())
