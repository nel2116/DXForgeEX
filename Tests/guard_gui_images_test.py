# Tests/guard_gui_images.py の回帰テスト(#195)。
#
#   python Tests/guard_gui_images_test.py
#
# フック(--hook)は、Claude Code と同じく stdin に JSON を渡して別プロセスとして確かめる。
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import guard_gui_images as guard  # noqa: E402

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "guard_gui_images.py")
WORKER_TEXT = "You are working inside Orca, a multi-agent IDE. You are a dispatched worker.\nYour task ID is: t"
TEMP = tempfile.gettempdir()
GUI_IMAGE = os.path.join(TEMP, "gui191", "pr-a.png")


def user(text):
    return {"type": "user", "message": {"role": "user", "content": text}}


def tool(name, **tool_input):
    return {"type": "assistant", "message": {"role": "assistant", "content": [{"type": "tool_use", "name": name, "input": tool_input}]}}


def run_hook(payload):
    data = payload if isinstance(payload, bytes) else json.dumps(payload).encode("utf-8")
    result = subprocess.run([sys.executable, SCRIPT, "--hook"], input=data, capture_output=True)
    return result.returncode, result.stderr.decode("utf-8", "replace")


class Paths(unittest.TestCase):
    def test_gui_check_images(self):
        for path in (GUI_IMAGE, os.path.join(TEMP, "gui-check-pr-200", "x.PNG"), os.path.join(TEMP, "GuiCheck", "sub", "a.jpg")):
            self.assertTrue(guard.is_gui_check_image(path), path)

    def test_other_files(self):
        for path in (
            os.path.join(TEMP, "gui191", "summary.md"),
            os.path.join(TEMP, "other", "a.png"),
            os.path.join(TEMP, "a.png"),
            r"F:\gui191\a.png",
            "",
            None,
        ):
            self.assertFalse(guard.is_gui_check_image(path), path)


class Hook(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="dxforge_gui_guard_test_")
        self.addCleanup(shutil.rmtree, self.dir, True)

    def transcript(self, *entries):
        path = os.path.join(self.dir, "transcript.jsonl")
        with open(path, "w", encoding="utf-8") as file:
            file.write('{"type":"session-owner"}\nnot json with tool_use orchestration\n')
            for entry in entries:
                file.write(json.dumps(entry) + "\n")
        return path

    def read(self, transcript, path=GUI_IMAGE):
        return run_hook({"tool_name": "Read", "tool_input": {"file_path": path}, "transcript_path": transcript})

    def test_coordinator_is_blocked_with_the_way_out(self):
        for entry in (
            tool("Skill", skill="orchestration"),
            tool("PowerShell", command="./StartWorker.ps1 -SpecFile a.md -Name x"),
            tool("Bash", command="orca orchestration worker-start --spec-file a"),
            tool("PowerShell", command="orca orchestration run-create --objective x"),
            tool("PowerShell", command="./.claude/skills/gui-check/start-gui-check.ps1 -Pr 5"),
        ):
            code, err = self.read(self.transcript(user("hello"), entry))
            self.assertEqual(code, 2, entry)
            self.assertIn("start-gui-check.ps1", err)

    def test_coordinator_may_read_other_files(self):
        transcript = self.transcript(user("hello"), tool("Skill", skill="orchestration"))
        self.assertEqual(self.read(transcript, os.path.join(TEMP, "other", "a.png"))[0], 0)
        self.assertEqual(self.read(transcript, os.path.join(TEMP, "gui191", "a.md"))[0], 0)

    def test_worker_is_not_blocked(self):
        transcript = self.transcript(user(WORKER_TEXT), tool("Skill", skill="orchestration"))
        self.assertEqual(self.read(transcript)[0], 0)

    def test_plain_conversation_is_not_blocked(self):
        for entry in (
            tool("PowerShell", command="git status"),
            tool("Bash", command="grep -n foo StartWorker.ps1"),
            tool("Skill", skill="gui-check"),
            user("orca orchestration worker-start はどう使う?"),
        ):
            self.assertEqual(self.read(self.transcript(user("hello"), entry))[0], 0, entry)

    def test_subagent_records_do_not_count(self):
        entry = dict(tool("Skill", skill="orchestration"), isSidechain=True)
        self.assertEqual(self.read(self.transcript(user("hello"), entry))[0], 0)

    def test_unknown_conversation_is_not_blocked(self):
        self.assertEqual(self.read(os.path.join(self.dir, "missing.jsonl"))[0], 0)
        self.assertEqual(self.read(None)[0], 0)

    def test_broken_input_is_not_blocked(self):
        self.assertEqual(run_hook(b"not json")[0], 0)
        self.assertEqual(run_hook(b"[]")[0], 0)
        self.assertEqual(run_hook({"tool_input": None})[0], 0)


if __name__ == "__main__":
    unittest.main()
