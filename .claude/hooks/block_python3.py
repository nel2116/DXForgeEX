# Claude Code の PreToolUse フック(Bash / PowerShell): python3 の実行を止めて python を使わせる。
#
# このマシンの python3 は Windows ストアのスタブで、何も実行せずに黙って終わる。テストが「通った」ように
# 見えてしまうので、文章のルールではなくフックで止める。
#
# シェルの構文(コマンド置換の入れ子、環境変数の前置など)を正規表現で完全に追うのは無理なので、迷ったら止める側に
# 倒す: 中身が決して実行されない部分(単一引用符の中、引用符付きのヒアドキュメント、PowerShell の @'...'@)だけを
# 取り除き、残りに単語としての python3 があれば止める。二重引用符の中の文章も止まるが、メッセージで
# 単一引用符か <<'EOF' で囲むよう案内する(見逃すより、書き直し1回のほうが安い)。
import json
import re
import sys

# 引用符付きのヒアドキュメント(<<'EOF' / <<"EOF")の本文と PowerShell の @'...'@ は展開されない。
# ヒアドキュメントを始める行の残り(cat <<'EOF' && python3 x.py の && 以降)は実行されるので残す
QUOTED_HEREDOC = re.compile(r"<<-?\s*(['\"])(\w+)\1([^\n]*)\n.*?^\s*\2\s*$", re.DOTALL | re.MULTILINE)
PS_LITERAL_HERESTRING = re.compile(r"@'\r?\n.*?^'@", re.DOTALL | re.MULTILINE)
WORD = re.compile(r"(?<![\w.-])python3(?:\.exe)?(?![\w.-])")


def strip_single_quoted(command):
    # 単一引用符の中を取り除く。二重引用符の中の ' は文字そのもので、\ の直後の文字は引用符として扱わない
    out = []
    i, n, in_double = 0, len(command), False
    while i < n:
        c = command[i]
        if c == "\\" and i + 1 < n:
            out.append(command[i:i + 2])
            i += 2
        elif c == "\"":
            in_double = not in_double
            out.append(c)
            i += 1
        elif c == "'" and not in_double:
            j = command.find("'", i + 1)
            end = n if j < 0 else j
            # 'python3' や & 'python3.exe' のように、引用符で囲んでもコマンド名としては実行される
            out.append(" " + command[i + 1:end] + " " if WORD.fullmatch(command[i + 1:end]) else " ")
            i = end + 1
        else:
            out.append(c)
            i += 1
    return "".join(out)


def runs_python3(command):
    command = QUOTED_HEREDOC.sub(lambda m: " " + m.group(3) + "\n", command)
    command = PS_LITERAL_HERESTRING.sub(" ", command)
    return WORD.search(strip_single_quoted(command)) is not None


def main():
    try:
        # Claude Code は UTF-8 で渡す。コンソールのコードページ(CP932)で読むとパスや本文が化けるのでバイトで読む
        payload = json.loads(sys.stdin.buffer.read())
    except ValueError:
        return 0
    command = (payload.get("tool_input") or {}).get("command") or ""
    if runs_python3(command):
        print("python3 はこのマシンでは Windows ストアのスタブで、何も実行されません。python を使ってください。"
              "実行せずに文字として書きたいだけなら、単一引用符か <<'EOF' のヒアドキュメントで囲んでください。",
              file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.stderr.reconfigure(encoding="utf-8")
    sys.exit(main())
