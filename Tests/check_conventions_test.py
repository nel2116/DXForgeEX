# Tests/check_conventions.py の回帰テスト。
#
#   python Tests/check_conventions_test.py
#
# 規則ごとに「違反を見つけること」と「正しいものを違反にしないこと」の両方を確かめる。
# フック(--hook)と --fix は、リポジトリの Tests/ の下に作る使い捨てのフォルダで、別プロセスとして確かめる。
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import check_conventions as cc  # noqa: E402

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "check_conventions.py")
HEADER = "// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/\n// [Test.cpp]\n// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/\n"


def cpp(body):
    return cc.BOM + (HEADER + body).encode("utf-8")


def rules(rel, data):
    return [(rule, ln) for rule, ln, _ in cc.check_file(rel, data)]


class NarrowLiteralTest(unittest.TestCase):
    def narrow(self, body):
        return [ln for ln, _ in cc.find_narrow_non_ascii(body)]

    def test_detects_narrow_string(self):
        self.assertEqual(self.narrow('int a;\nOutputDebugStringA("初期化に失敗");\n'), [2])

    def test_detects_narrow_char_and_raw_string(self):
        self.assertEqual(self.narrow("char c = 'あ';\n"), [1])
        self.assertEqual(self.narrow('auto s = R"x(\n日本語\n)x";\nauto t = "あ";\n'), [1, 4])

    def test_ignores_wide_and_utf8_literals(self):
        self.assertEqual(self.narrow('L"あ"; u"あ"; U"あ"; u8"あ"; LR"(あ)"; L\'あ\';'), [])

    def test_ignores_comments(self):
        self.assertEqual(self.narrow('// "あ"\n/* "い"\n "う" */ int x = 1;\n'), [])

    def test_escapes_and_digit_separators(self):
        self.assertEqual(self.narrow('auto a = "\\"あ";'), [1])
        self.assertEqual(self.narrow('auto a = "\\\\"; // "あ"\n'), [])
        self.assertEqual(self.narrow("int n = 1'000'000; auto s = L\"あ\";"), [])


class CheckFileTest(unittest.TestCase):
    def test_clean_cpp(self):
        self.assertEqual(rules("DXForgeEngine/A.cpp", cpp('auto s = L"あ";\n')), [])

    def test_cpp_violations(self):
        self.assertEqual(rules("DXForgeEngine/A.cpp", (HEADER + "int x;\n").encode("utf-8")), [("bom", 1)])
        self.assertEqual(rules("DXForgeEngine/A.h", cc.BOM + b"#pragma once\n"), [("header", 1)])
        self.assertEqual(rules("DXForgeEngine/A.cpp", cpp('\nauto s = "あ";\n')), [("narrow", 5)])

    def test_header_exemptions(self):
        self.assertEqual(rules("DXForgeEngineDLL/pch.h", cc.BOM + b"#pragma once\n"), [])

    def test_bom_required_for_cs_xaml_ps1(self):
        for rel in ("DXForgeEditor/A.cs", "DXForgeEditor/A.xaml", "A.ps1"):
            self.assertEqual(rules(rel, b"x"), [("bom", 1)], rel)
            self.assertEqual(rules(rel, cc.BOM + b"x"), [], rel)

    def test_hlsl(self):
        ja = "// 日本語\n".encode("utf-8")
        self.assertEqual(rules("DXForgeEngine/Graphics/Direct3D12/Shaders/A.hlsl", ja), [("bom-hlsl", 1)])
        self.assertEqual(rules("DXForgeEngine/Graphics/Direct3D12/Shaders/A.hlsl", b"// ascii\n"), [])
        self.assertEqual(rules("EngineTest/A.hlsli", cc.BOM + ja), [])
        self.assertEqual(rules("ContentTools/A.hlsl", cc.BOM + b"x"), [("no-bom", 1)])
        self.assertEqual(rules("DXForgeEditor/Resources/A.hlsl", ja), [])

    def test_targets(self):
        self.assertTrue(cc.is_target("DXForgeEngine/A.cpp"))
        self.assertFalse(cc.is_target("packages/X/a.h"))
        self.assertFalse(cc.is_target("README.md"))

    def test_paths_are_case_insensitive(self):
        # Windows では contenttools/... でも同じファイルを指すので、同じ規則を選ぶ
        self.assertEqual(rules("contenttools/A.hlsl", cc.BOM + b"x"), [("no-bom", 1)])
        self.assertEqual(rules("dxforgeenginedll/PCH.h", cc.BOM + b"#pragma once\n"), [])
        self.assertFalse(cc.is_target("Packages/X/a.h"))


class ProcessTest(unittest.TestCase):
    # フックと --fix を別プロセスで確かめる。相対パスの判定に使うので、フォルダはリポジトリの中に作る
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="_conventions_test_", dir=os.path.dirname(SCRIPT))

    def tearDown(self):
        shutil.rmtree(self.dir, ignore_errors=True)

    def write(self, name, data):
        path = os.path.join(self.dir, name)
        with open(path, "wb") as f:
            f.write(data)
        return path

    def hook(self, path):
        # Claude Code(Node の JSON.stringify)と同じく、非ASCII文字をエスケープせず UTF-8 で渡す
        payload = json.dumps({"tool_name": "Write", "tool_input": {"file_path": path}}, ensure_ascii=False)
        return subprocess.run([sys.executable, SCRIPT, "--hook"], input=payload.encode("utf-8"),
                              capture_output=True)

    def test_hook_blocks_violation_with_reason(self):
        path = self.write("A.cpp", (HEADER + 'auto s = "あ";\n').encode("utf-8"))
        r = self.hook(path)
        self.assertEqual(r.returncode, 2)
        err = r.stderr.decode("utf-8")
        self.assertIn("[bom]", err)
        self.assertIn("[narrow]", err)
        self.assertIn("--fix", err)

    def test_hook_passes_clean_unrelated_and_outside_files(self):
        self.assertEqual(self.hook(self.write("A.cpp", cpp("int x;\n"))).returncode, 0)
        self.assertEqual(self.hook(self.write("notes.md", b"x")).returncode, 0)
        outside = tempfile.mkdtemp()
        try:
            path = os.path.join(outside, "A.cpp")
            with open(path, "wb") as f:
                f.write(b"int x;\n")
            self.assertEqual(self.hook(path).returncode, 0)
        finally:
            shutil.rmtree(outside, ignore_errors=True)
        self.assertEqual(subprocess.run([sys.executable, SCRIPT, "--hook"], input=b"not json").returncode, 0)

    def test_hook_reads_utf8_json_with_japanese_path(self):
        # Claude Code は UTF-8 の JSON を渡す。コンソールのコードページ(CP932/1252)で読むとパスが化けて見逃す
        sub = os.path.join(self.dir, "作業")
        os.mkdir(sub)
        path = os.path.join(sub, "A.cpp")
        with open(path, "wb") as f:
            f.write((HEADER + "int x;\n").encode("utf-8"))
        r = self.hook(path)
        self.assertEqual(r.returncode, 2, r.stderr.decode("utf-8", errors="replace"))
        self.assertIn("[bom]", r.stderr.decode("utf-8"))

    def test_fix_adds_bom(self):
        path = self.write("A.cpp", (HEADER + "int x;\n").encode("utf-8"))
        r = subprocess.run([sys.executable, SCRIPT, "--fix", path], capture_output=True)
        self.assertEqual(r.returncode, 0, r.stdout.decode("utf-8") + r.stderr.decode("utf-8"))
        with open(path, "rb") as f:
            self.assertTrue(f.read().startswith(cc.BOM))


class BlockPython3HookTest(unittest.TestCase):
    # .claude/hooks/block_python3.py(PreToolUse)。実行する python3 だけを止め、引数や本文の中の python3 は止めない
    HOOK = os.path.join(os.path.dirname(SCRIPT), "..", ".claude", "hooks", "block_python3.py")

    def exit_code(self, command):
        payload = json.dumps({"tool_name": "Bash", "tool_input": {"command": command}}, ensure_ascii=False)
        return subprocess.run([sys.executable, self.HOOK], input=payload.encode("utf-8"),
                              capture_output=True).returncode

    def test_blocks_running_python3(self):
        for command in ("python3 x.py", "cd a && python3 -c 1", "echo $(python3 -V)", "python3.exe -V",
                        "cat <<'EOF'\nx\nEOF\npython3 y.py",
                        # 引用符の中でも、コマンド置換は実際に実行される
                        "PYTHONUTF8=1 python3 x.py", "A=1 B=2 python3 x.py", "env PYTHONUTF8=1 python3 x.py",
                        "time python3 x.py",
                        'output="$(python3 x.py 2>&1)"', 'echo "`python3 -V`"', "cat <<EOF\n$(python3 -V)\nEOF",
                        '$v = @"\n$(python3 -V)\n"@',
                        'result="$(python3 "Tests/check_conventions.py")"', "x=$(echo 'a' && python3 -V)",
                        "nohup env A=1 python3 x.py &",
                        # 迷ったら止める側: 二重引用符の中の文章も止める(単一引用符か <<'EOF' で書けば通る)
                        'git commit -m "python3 は使わない"', 'echo "it\'s python3"'):
            self.assertEqual(self.exit_code(command), 2, command)

    def test_allows_python_and_quoted_mentions(self):
        for command in ("python x.py", "git grep -n -E '(python3|python)'", "git commit -m 'python3 は使わない'",
                        "git commit -F - <<'EOF'\npython3 は使わない\nEOF", "$m = @'\npython3 -V\n'@",
                        "cat <<\"EOF\"\n$(python3 -V)\nEOF", "echo '$(python3 -V)'",
                        "gh pr comment 1 --body 'it uses \"python3\" and $(python3 x)'",
                        "echo 日本語 && python -V", "PYTHONUTF8=1 python x.py", "pip show python3-foo",
                        "python Tests/python3_notes.py", "ls python3.12"):
            self.assertEqual(self.exit_code(command), 0, command)


if __name__ == "__main__":
    unittest.main(verbosity=1)
