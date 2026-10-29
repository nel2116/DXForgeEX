# Claude Code の PreToolUse フック(Bash / PowerShell): python3 の実行を止めて python を使わせる。
#
# このマシンの python3 は Windows ストアのスタブで、何も実行せずに黙って終わる。テストが「通った」ように
# 見えてしまうので、文章のルールではなくフックで止める。コマンドとして実行する位置の python3 だけを対象にし、
# コミットメッセージなどの文中の "python3" は止めない。
import json
import re
import sys

PATTERN = re.compile(r"(?:^|[;&|(\n]|\$\()\s*python3(?:\.exe)?(?=\s|$|[;&|)])")


def main():
    try:
        payload = json.load(sys.stdin)
    except ValueError:
        return 0
    command = (payload.get("tool_input") or {}).get("command") or ""
    if PATTERN.search(command):
        print("python3 はこのマシンでは Windows ストアのスタブで、何も実行されません。python を使ってください。",
              file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.stderr.reconfigure(encoding="utf-8")
    sys.exit(main())
