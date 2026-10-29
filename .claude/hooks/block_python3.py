# Claude Code の PreToolUse フック(Bash / PowerShell): python3 の実行を止めて python を使わせる。
#
# このマシンの python3 は Windows ストアのスタブで、何も実行せずに黙って終わる。テストが「通った」ように
# 見えてしまうので、文章のルールではなくフックで止める。コマンドとして実行する位置の python3 だけを対象にし、
# 引用符・ヒアドキュメント・PowerShell のヒア文字列の中("git grep 'python3'" やコミットメッセージ)は止めない。
import json
import re
import sys

PATTERN = re.compile(r"(?:^|[;&|(\n]|\$\()\s*python3(?:\.exe)?(?=\s|$|[;&|)])")
# 先に取り除く部分(順番に適用する): ヒアドキュメント、PowerShell のヒア文字列、引用符で囲んだ文字列
LITERALS = [
    re.compile(r"<<-?\s*(['\"]?)(\w+)\1[^\n]*\n.*?^\s*\2\s*$", re.DOTALL | re.MULTILINE),
    re.compile(r"@(['\"])\r?\n.*?^\1@", re.DOTALL | re.MULTILINE),
    re.compile(r"'[^']*'|\"(?:\\.|[^\"\\])*\""),
]


def runs_python3(command):
    for pattern in LITERALS:
        command = pattern.sub(" ", command)
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
