# エージェントが PR に merge-ok ラベルを付けるのを止める、Claude Code の PreToolUse フック(#160)。
#
#   python Tests/guard_merge_approval.py --hook      stdin の JSON(Bash/PowerShell のコマンド)を読み、merge-ok を
#                                                    付けるコマンドなら stderr に理由を出して終了コード2(実行させない)
#   python Tests/guard_merge_approval.py "<command>" コマンドを判定して結果を表示する(確認用)
#
# merge-ok は、人がマージを承認するラベルである(.github/workflows/pr-automation.yml の Merge approved)。
# エージェントはユーザーと同じ GitHub アカウント(gh)で動くので、GitHub の側では人とエージェントを区別できない。
# そのため、エージェントが付けられないようにここで止める。止めるのは gh のコマンドだけで、スクリプトのファイルを
# 経由したものや curl などは見ない(安全の境界ではなく、うっかり付けることを防ぐためのもの)。
import json
import re
import sys

LABEL = "merge-ok"

# gh の呼び出しを、次のコマンドの区切り(; && || | 改行)までで切り出す
_GH_CALL = re.compile(r"(?<![\w.-])gh(?:\.exe)?\s+[^;&|\r\n]*")
_PR_OR_ISSUE_WRITE = re.compile(r"^gh(?:\.exe)?\s+(?:pr|issue)\s+(?:edit|create)\b")
_LABEL_OPTION = re.compile(r"(?<![\w-])(?:--add-label|--label|-l)(?![\w-])")
_API = re.compile(r"^gh(?:\.exe)?\s+api\b")
_DELETE = re.compile(r"(?:-X|--method)[\s=]*['\"]?DELETE\b", re.IGNORECASE)


def adds_merge_ok(command: str) -> bool:
    """コマンドに、PR(Issue)へ merge-ok ラベルを付ける gh の呼び出しが含まれていれば True。"""
    for match in _GH_CALL.finditer(command):
        call = match.group(0)
        if LABEL not in call:
            continue
        # gh pr edit --add-label merge-ok / gh pr create --label merge-ok など
        if _PR_OR_ISSUE_WRITE.match(call) and _LABEL_OPTION.search(call):
            return True
        # gh api repos/{owner}/{repo}/issues/<N>/labels -f "labels[]=merge-ok" など(DELETE で外すのは止めない)
        if _API.match(call) and "labels" in call and not _DELETE.search(call):
            return True
    return False


MESSAGE = (
    "merge-ok は、人がマージを承認するためのラベルです。エージェントは付けません(#160)。\n"
    "マージの準備ができたら、承認待ちであることを PR の番号とともにユーザーに報告してください。"
)


def run_hook() -> int:
    try:
        payload = json.loads(sys.stdin.buffer.read())
    except ValueError:
        return 0
    command = (payload.get("tool_input") or {}).get("command") or ""
    if adds_merge_ok(command):
        print(MESSAGE, file=sys.stderr)
        return 2
    return 0


def main(argv) -> int:
    if "--hook" in argv:
        return run_hook()
    for command in argv:
        print(f"{'BLOCK' if adds_merge_ok(command) else 'allow'}: {command}")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    sys.exit(main(sys.argv[1:]))
