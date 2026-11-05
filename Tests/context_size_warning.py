# 会話の文脈が大きくなったら、新しい会話への引き継ぎを 1 回だけ促す、Claude Code の PostToolUse フック(#195)。
#
#   python Tests/context_size_warning.py --hook   stdin の JSON の transcript_path を読み、超えていれば
#                                                 additionalContext で警告する。止めることはない(終了コードは常に 0)
#
# 消費は「ターン数 × その時点の文脈の大きさ」で増える(#195 の集計では、1 ターンあたり約 22 万トークン)。
# 文脈の大きさは、最後のアシスタントのメッセージの usage(input + cache_read + cache_creation)で測る。
# ツールの呼び出しごとに動くので、記録の末尾だけを読む。末尾に usage が見つからないときは何もしない
# (巨大な画像の行など。次の呼び出しで見つかる)。
#
# しきい値は環境変数 DXFORGE_CONTEXT_WARN_TOKENS(既定 150000、0 以下で無効)。
# 警告した印は %TEMP%\dxforge-context-warning\<session_id> に置く。印を作れないときは、同じ警告を毎回出さないよう、
# 警告しない。
import json
import os
import re
import sys
import tempfile

DEFAULT_THRESHOLD = 150000
_TAIL_BYTES = 256 * 1024


def threshold_from_env() -> int:
    try:
        return int(os.environ.get("DXFORGE_CONTEXT_WARN_TOKENS", DEFAULT_THRESHOLD))
    except ValueError:
        return DEFAULT_THRESHOLD


def last_context_tokens(transcript_path):
    """記録の末尾から、最後のアシスタントのメッセージの文脈の大きさを返す。見つからなければ None。"""
    try:
        with open(transcript_path, "rb") as file:
            file.seek(0, os.SEEK_END)
            size = file.tell()
            file.seek(max(0, size - _TAIL_BYTES))
            tail = file.read()
    except (OSError, TypeError):
        return None
    lines = tail.split(b"\n")
    if size > _TAIL_BYTES:
        lines = lines[1:]  # 先頭の行は途中から読んでいる
    for line in reversed(lines):
        if b'"assistant"' not in line:
            continue
        try:
            entry = json.loads(line)
        except ValueError:
            continue
        if not isinstance(entry, dict) or entry.get("type") != "assistant" or entry.get("isSidechain"):
            continue
        message = entry.get("message")
        usage = message.get("usage") if isinstance(message, dict) else None
        if not isinstance(usage, dict):
            continue
        total = sum(usage.get(key) or 0 for key in
                    ("input_tokens", "cache_read_input_tokens", "cache_creation_input_tokens"))
        # API を呼ばずに作られたメッセージ(usage がすべて 0)は測定値ではない
        if total > 0:
            return total
    return None


def mark_warned(session_id) -> bool:
    """この会話で初めてなら印を作って True を返す。すでに印があるか、作れなければ False。"""
    name = re.sub(r"[^\w.-]", "_", str(session_id or "unknown"))
    folder = os.path.join(tempfile.gettempdir(), "dxforge-context-warning")
    try:
        os.makedirs(folder, exist_ok=True)
        os.close(os.open(os.path.join(folder, name), os.O_CREAT | os.O_EXCL | os.O_WRONLY))
    except OSError:
        return False
    return True


def run_hook() -> int:
    try:
        payload = json.loads(sys.stdin.buffer.read())
    except ValueError:
        return 0
    limit = threshold_from_env()
    if not isinstance(payload, dict) or limit <= 0:
        return 0
    tokens = last_context_tokens(payload.get("transcript_path"))
    if tokens is None or tokens <= limit or not mark_warned(payload.get("session_id")):
        return 0
    message = (
        f"この会話の文脈が約 {tokens:,} トークンになりました(しきい値 {limit:,})。"
        "消費は「ターン数 × 文脈の大きさ」で増えるので、区切りのよいところで新しい会話に引き継いでください"
        "(コミットして push し、残りの作業を書き出す)。この警告は、この会話では 1 回だけ出します(#195)。"
    )
    print(json.dumps({"hookSpecificOutput": {"hookEventName": "PostToolUse", "additionalContext": message}},
                     ensure_ascii=False))
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    if "--hook" in sys.argv[1:]:
        sys.exit(run_hook())
    print("usage: context_size_warning.py --hook", file=sys.stderr)
    sys.exit(2)
