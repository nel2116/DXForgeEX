# DXForgeEX のファイル規約を機械的に確かめる(AGENTS.md の「文章のルール」を強制する側)。
#
# 確かめること(どれも破ると、ビルドが壊れるか一見無関係なエラーになる実害がある):
#   bom       C/C++・C#・XAML・PowerShell は UTF-8 BOM が必須。
#             BOM が無いと MSVC がローカルコードページ(Shift-JIS)と誤認し、日本語コメント以降が壊れる。
#   bom-hlsl  エンジン側の HLSL は、非ASCII文字を含むなら BOM が必須(SharedTypes.h 経由で MSVC も読むため)。
#   no-bom    ContentTools/ と DXForgeEditor/ の HLSL は fxc でコンパイルするので BOM 禁止
#             (fxc は "error X3000: Illegal character in shader file" で拒否する)。
#   narrow    C/C++ のナロー文字列/文字リテラルに非ASCII文字を入れない。CI ランナーのコードページは 1252 で、
#             1文字ごとに C4566 が出て /WX のビルドが落ちる。L"..."(ワイド)を使う。コメントは対象外。
#   header    C/C++ ファイルは日本語のヘッダーコメント(// _/_/_/...)で始める。外部コードと VS の生成物は除外。
#
# 使い方:
#   python Tests/check_conventions.py                  git で管理しているファイルすべて(CI)
#   python Tests/check_conventions.py <path>...        指定したファイルだけ
#   python Tests/check_conventions.py --fix <path>...  BOM の付け外しだけ自動で直す(他の違反は手で直す)
#   python Tests/check_conventions.py --hook           Claude Code の PostToolUse フック。stdin の JSON から
#                                                      編集されたファイルを読み、違反があれば stderr に出して終了コード2
# 終了コード: 0 違反なし / 1 違反あり / 2 フックで違反あり(Claude Code はこれを Claude に返す)
import json
import os
import subprocess
import sys

REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
BOM = b"\xef\xbb\xbf"

CPP_EXTS = {"c", "cpp", "h", "hpp"}
BOM_EXTS = CPP_EXTS | {"cs", "xaml", "ps1"}
HLSL_EXTS = {"hlsl", "hlsli"}
FXC_DIRS = ("ContentTools/", "DXForgeEditor/")
# ヘッダーコメントの対象外: 外部コード(MikkTSpace)と、Visual Studio が生成したファイル
HEADER_EXEMPT = {
    "ContentTools/MikkTSpace/mikktspace.c",
    "ContentTools/MikkTSpace/mikktspace.h",
    "DXForgeEngineDLL/framework.h",
    "DXForgeEngineDLL/pch.cpp",
    "DXForgeEngineDLL/pch.h",
}
SKIP_DIRS = ("packages/", "x64/", "DebugEditor/", "ReleaseEditor/")
# このプレフィックスが付いたリテラルはコードページに依存しない(L/u/U は UTF-16/32、u8 は UTF-8)
WIDE_PREFIXES = {"L", "u", "U", "u8", "LR", "uR", "UR", "u8R"}


def ext_of(rel):
    name = rel.rsplit("/", 1)[-1]
    return name.rsplit(".", 1)[-1].lower() if "." in name else ""


# Windows のパスは大文字・小文字を区別しないので、規則の選択も区別しない(contenttools/... でも fxc 扱い)
def under(rel, prefixes):
    return rel.casefold().startswith(tuple(p.casefold() for p in prefixes))


def is_fxc(rel):
    return ext_of(rel) in HLSL_EXTS and under(rel, FXC_DIRS)


def is_target(rel):
    return not under(rel, SKIP_DIRS) and ext_of(rel) in (BOM_EXTS | HLSL_EXTS)


def wants_bom(rel, data):
    ext = ext_of(rel)
    if ext in HLSL_EXTS:
        return not is_fxc(rel) and any(b > 127 for b in data)
    return ext in BOM_EXTS


def find_narrow_non_ascii(text):
    # C/C++ のソースから、非ASCII文字を含むナロー文字列/文字リテラルの (行番号, リテラルの先頭) を返す
    results = []
    i, n, line = 0, len(text), 1
    while i < n:
        c = text[i]
        if c == "\n":
            line += 1
            i += 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            line += text.count("\n", i, j)
            i = j
        elif c in "\"'":
            # 直前の識別子がプレフィックス(L, u8, R など)かを調べる
            k = i
            while k > 0 and (text[k - 1].isalnum() or text[k - 1] == "_"):
                k -= 1
            prefix = text[k:i]
            if c == "'" and prefix and prefix not in WIDE_PREFIXES:
                i += 1  # 1'000'000 のような桁区切り
                continue
            if c == "\"" and prefix.endswith("R"):
                # 生文字列リテラル R"delim( ... )delim"
                p = text.find("(", i)
                delim = text[i + 1:p]
                end = text.find(")" + delim + "\"", p)
                end = n if end < 0 else end + len(delim) + 2
            else:
                j = i + 1
                while j < n and text[j] != c and text[j] != "\n":
                    j += 2 if text[j] == "\\" else 1
                end = min(j + 1, n)
            body = text[i:end]
            if prefix not in WIDE_PREFIXES and any(ord(ch) > 127 for ch in body):
                results.append((line, (prefix + body)[:40]))
            line += body.count("\n")
            i = end
        else:
            i += 1
    return results


def check_file(rel, data):
    # 1ファイルの違反を (規則, 行番号, メッセージ) のリストで返す
    ext = ext_of(rel)
    has_bom = data.startswith(BOM)
    fix = f"直し方: python Tests/check_conventions.py --fix {rel}"
    violations = []
    if ext in BOM_EXTS and not has_bom:
        violations.append(("bom", 1, f"UTF-8 BOM がありません(MSVC などが Shift-JIS と誤認する)。{fix}"))
    if ext in HLSL_EXTS:
        if is_fxc(rel):
            if has_bom:
                violations.append(("no-bom", 1, f"fxc でコンパイルする HLSL に BOM があります(fxc が X3000 で拒否する)。{fix}"))
        elif not has_bom and wants_bom(rel, data):
            violations.append(("bom-hlsl", 1, f"非ASCII文字を含むエンジン側の HLSL に BOM がありません。{fix}"))
    if ext in CPP_EXTS:
        # CRLF を LF にそろえる(CRLF のままだと "abc\ の行継続で \ が CR だけを飛ばし、文字列の終わりと誤認する)
        text = data.decode("utf-8-sig", errors="replace").replace("\r\n", "\n")
        for ln, lit in find_narrow_non_ascii(text):
            violations.append(("narrow", ln, f"ナローリテラルに非ASCII文字があります: {lit} "
                                             "(CI のコードページ1252で C4566 → /WX で失敗する)。"
                                             "L\"...\" とワイド版のAPI(例: OutputDebugStringW)を使ってください。"))
        if rel.casefold() not in {p.casefold() for p in HEADER_EXEMPT} and not text.lstrip().startswith("// _/_/"):
            violations.append(("header", 1, "日本語のヘッダーコメント(// _/_/_/... [ファイル名] 作成日/作成者/概要/更新履歴)"
                                            "で始まっていません。既存の .cpp/.h の先頭をまねてください。"))
    return violations


def fix_bom(rel, path):
    with open(path, "rb") as f:
        data = f.read()
    has_bom = data.startswith(BOM)
    if wants_bom(rel, data) and not has_bom:
        with open(path, "wb") as f:
            f.write(BOM + data)
        return "BOM を付けました"
    if is_fxc(rel) and has_bom:
        with open(path, "wb") as f:
            f.write(data[len(BOM):])
        return "BOM を外しました"
    return None


def to_rel(path):
    full = os.path.normpath(os.path.abspath(path))
    try:
        inside = os.path.normcase(os.path.commonpath([full, REPO_ROOT])) == os.path.normcase(REPO_ROOT)
    except ValueError:  # 別のドライブ
        inside = False
    if not inside:
        return None, full
    return os.path.relpath(full, REPO_ROOT).replace("\\", "/"), full


def git_files():
    out = subprocess.run(["git", "ls-files", "-z"], cwd=REPO_ROOT, capture_output=True, check=True).stdout
    return [p.decode("utf-8") for p in out.split(b"\0") if p]


def read(path):
    with open(path, "rb") as f:
        return f.read()


def run_hook():
    try:
        # Claude Code は UTF-8 で渡す。コンソールのコードページ(CP932)で読むと日本語のパスが化けるのでバイトで読む
        payload = json.loads(sys.stdin.buffer.read())
    except ValueError:
        return 0
    path = (payload.get("tool_input") or {}).get("file_path")
    if not path:
        return 0
    rel, full = to_rel(path)
    if rel is None or not is_target(rel) or not os.path.isfile(full):
        return 0
    violations = check_file(rel, read(full))
    for rule, ln, msg in violations:
        print(f"{rel}:{ln}: [{rule}] {msg}", file=sys.stderr)
    return 2 if violations else 0


def main(argv):
    if "--hook" in argv:
        return run_hook()
    fix = "--fix" in argv
    paths = [a for a in argv if not a.startswith("--")]
    if paths:
        targets = [to_rel(p) for p in paths]
    else:
        targets = [(rel, os.path.join(REPO_ROOT, rel)) for rel in git_files()]
    count = 0
    for rel, full in targets:
        if rel is None or not is_target(rel) or not os.path.isfile(full):
            continue
        if fix:
            done = fix_bom(rel, full)
            if done:
                print(f"{rel}: {done}")
        for rule, ln, msg in check_file(rel, read(full)):
            print(f"{rel}:{ln}: [{rule}] {msg}")
            count += 1
    if count:
        print(f"{count} 件の規約違反があります。", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    sys.exit(main(sys.argv[1:]))
