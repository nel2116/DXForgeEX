# コーディネーターの会話で GUI 確認の画像を Read するのを止める、Claude Code の PreToolUse フック(#195)。
#
#   python Tests/guard_gui_images.py --hook      stdin の JSON(Read の file_path)を読み、止めるときは
#                                                stderr に理由を出して終了コード2(実行させない)
#
# 画像は文脈に残り、毎ターン読み直される。GUI 確認は start-gui-check.ps1 で新しいワーカーに任せて、結果の要約だけを受け取る。
#
# 止める条件(すべて満たすとき):
#   1. 拡張子が画像で、%TEMP% の下の gui で始まるフォルダの中のファイル(例: %TEMP%\gui191\pr-a.png、%TEMP%\gui-check-pr-200\x.png)。
#   2. ワーカーの会話でない(guard_review_flow.py と同じ、最初のユーザーメッセージの前書きで見分ける)。
#   3. コーディネーターの会話である。その会話が orchestration スキルを使った、またはワーカーを起動した記録(tool_use)があること。
# 確実に見分けられないとき(記録が読めない、コーディネーターの印がない)は止めない。普通の会話でユーザーが画像を見せるよう
# 頼んだ場合を止めないため。安全の境界ではなく、うっかり読むことを防ぐためのもの。
import json
import os
import re
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from guard_review_flow import is_orchestration_worker  # noqa: E402

_IMAGE_EXTENSIONS = {".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp"}
_GUI_FOLDER = re.compile(r"^gui", re.IGNORECASE)
# ワーカーの起動。読むだけのコマンド(grep StartWorker.ps1 など)を拾わないよう、引数が続くものだけ
_STARTS_WORKER = re.compile(
    r"(?:StartWorker|start-gui-check)\.ps1\s+-\w|orca(?:\.exe)?\s+orchestration\s+(?:worker-start|run-create|dispatch)(?![\w-])",
    re.IGNORECASE,
)
# 速さのため、JSON として読む行をバイト列で先に絞る
_LINE_MARKERS = (b"tool_use",)
_NAME_MARKERS = (b"orchestration", b"StartWorker", b"start-gui-check")


def _temp_roots():
    roots = {os.environ.get("TEMP"), os.environ.get("TMP"), tempfile.gettempdir()}
    return [os.path.normcase(os.path.normpath(root)) for root in roots if root]


def is_gui_check_image(path) -> bool:
    """%TEMP% の下の gui で始まるフォルダの中の画像なら True。"""
    if not isinstance(path, str) or not path:
        return False
    if os.path.splitext(path)[1].lower() not in _IMAGE_EXTENSIONS:
        return False
    normal = os.path.normcase(os.path.normpath(path))
    for root in _temp_roots():
        prefix = root.rstrip(os.sep) + os.sep
        if normal.startswith(prefix):
            folders = normal[len(prefix):].split(os.sep)[:-1]
            return any(_GUI_FOLDER.match(folder) for folder in folders)
    return False


def _uses_orchestration(block) -> bool:
    if not isinstance(block, dict) or block.get("type") != "tool_use":
        return False
    tool_input = block.get("input")
    if not isinstance(tool_input, dict):
        return False
    if block.get("name") == "Skill":
        return tool_input.get("skill") == "orchestration"
    if block.get("name") in ("Bash", "PowerShell"):
        return _STARTS_WORKER.search(str(tool_input.get("command") or "")) is not None
    return False


def is_coordinator(transcript_path) -> bool:
    """記録に orchestration スキルの使用かワーカーの起動があれば True。読めなければ False(止めない)。"""
    if not transcript_path:
        return False
    try:
        with open(transcript_path, "rb") as file:
            for line in file:
                if not all(marker in line for marker in _LINE_MARKERS):
                    continue
                if not any(marker in line for marker in _NAME_MARKERS):
                    continue
                try:
                    entry = json.loads(line)
                except ValueError:
                    continue
                if not isinstance(entry, dict) or entry.get("type") != "assistant" or entry.get("isSidechain"):
                    continue
                message = entry.get("message")
                content = message.get("content") if isinstance(message, dict) else None
                if isinstance(content, list) and any(_uses_orchestration(block) for block in content):
                    return True
    except OSError:
        return False
    return False


MESSAGE = (
    "コーディネーターの会話では、GUI 確認の画像を読みません(#195。画像は文脈に残り、毎ターン読み直されます)。"
    "GUI 確認は ./.claude/skills/gui-check/start-gui-check.ps1 -Pr <N> で新しいワーカーに任せ、worker_done の要約を読んでください。"
    "画像は PR の本文に載っているので、人が確かめられます。"
)


def run_hook() -> int:
    try:
        payload = json.loads(sys.stdin.buffer.read())
    except ValueError:
        return 0
    if not isinstance(payload, dict):
        return 0
    if not is_gui_check_image((payload.get("tool_input") or {}).get("file_path")):
        return 0
    transcript = payload.get("transcript_path")
    if is_orchestration_worker(transcript) or not is_coordinator(transcript):
        return 0
    print(MESSAGE, file=sys.stderr)
    return 2


def main(argv) -> int:
    if "--hook" in argv:
        return run_hook()
    print("usage: guard_gui_images.py --hook", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    sys.exit(main(sys.argv[1:]))
