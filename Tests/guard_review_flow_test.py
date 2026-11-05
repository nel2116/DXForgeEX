# Tests/guard_review_flow.py の回帰テスト(#195)。
#
#   python Tests/guard_review_flow_test.py
#
# フック(--hook)は、Claude Code と同じく stdin に JSON を渡して別プロセスとして確かめる。
# スタンプの確認は %TEMP% の使い捨ての git リポジトリで行う。
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import guard_review_flow as guard  # noqa: E402

SCRIPT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "guard_review_flow.py")
WORKER_TEXT = "You are working inside Orca, a multi-agent IDE. You are a dispatched worker.\nYour task ID is: t"


def write_transcript(folder, *entries):
    path = os.path.join(folder, "transcript.jsonl")
    with open(path, "w", encoding="utf-8") as file:
        file.write('{"type":"session-owner"}\n{"type":"attachment"}\n')
        for entry in entries:
            file.write(json.dumps(entry) + "\n")
    return path


def user_message(content, **extra):
    return {"type": "user", "message": {"role": "user", "content": content}, **extra}


class TempDir(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.mkdtemp(prefix="dxforge_guard_test_")
        self.addCleanup(shutil.rmtree, self.dir, True)


class RunsSetStatus(unittest.TestCase):
    def test_detects_set_status(self):
        for command in [
            "./CodexReview.ps1 -Pr 12 -Post -SetStatus",
            "./CodexReview.ps1 -SetStatus -Pr 12",
            "pwsh -File CodexReview.ps1 -Pr 12 -setstatus",
            "& 'C:/w/CodexReview.ps1' -Pr 12 -SetStatus:$true",
            "./CodexReview -Pr 12 -SetS",
            "./CodexReview.ps1 -Pr 12 -S",
            "cd x; ./CodexReview.ps1 `\n  -Pr 12 -ForceReview -SetStatus",
        ]:
            with self.subTest(command=command):
                self.assertTrue(guard.runs_set_status(command))

    def test_allows_other_commands(self):
        for command in [
            "./CodexReview.ps1 -Pr 12 -Post",
            "./CodexReview.ps1 -Pr 12",
            "./CodexReview.ps1 -Pr 12 -Post -Effort low",
            "./Other.ps1 -SetStatus",
            "git diff --stat CodexReview.ps1",
        ]:
            with self.subTest(command=command):
                self.assertFalse(guard.runs_set_status(command))


class CreatesPr(unittest.TestCase):
    def test_detects_pr_create(self):
        for command in [
            "gh pr create --base develop --title t --body-file b.md",
            "GH.EXE pr create",
            "gh --repo nel2116/DXForgeEX pr create -B develop",
            "git push -u origin HEAD; gh pr create --fill",
            "gh pr  create",
        ]:
            with self.subTest(command=command):
                self.assertTrue(guard.creates_pr(command))

    def test_allows_other_commands(self):
        for command in ["gh pr view 12", "gh pr checks 12", "gh pr create-thing", "mygh pr create", "git push"]:
            with self.subTest(command=command):
                self.assertFalse(guard.creates_pr(command))


class Worker(TempDir):
    def test_worker_preamble_in_list_and_string_content(self):
        for content in [[{"type": "text", "text": WORKER_TEXT}], WORKER_TEXT]:
            path = write_transcript(self.dir, user_message(content))
            self.assertTrue(guard.is_orchestration_worker(path))

    def test_coordinator_and_later_mentions_are_not_workers(self):
        # 2 番目のメッセージや、長い本文の途中に前書きの文があっても、ワーカーとしない
        path = write_transcript(self.dir, user_message("start the run"), user_message(WORKER_TEXT))
        self.assertFalse(guard.is_orchestration_worker(path))
        path = write_transcript(self.dir, user_message("x" * 500 + WORKER_TEXT))
        self.assertFalse(guard.is_orchestration_worker(path))

    def test_sidechain_message_is_skipped(self):
        path = write_transcript(self.dir, user_message(WORKER_TEXT, isSidechain=True), user_message("hello"))
        self.assertFalse(guard.is_orchestration_worker(path))

    def test_unknown_means_not_a_worker(self):
        self.assertFalse(guard.is_orchestration_worker(None))
        self.assertFalse(guard.is_orchestration_worker(os.path.join(self.dir, "missing.jsonl")))
        broken = os.path.join(self.dir, "broken.jsonl")
        with open(broken, "wb") as file:
            file.write(b"\xff\xfe not json\n[1, 2]\n")
        self.assertFalse(guard.is_orchestration_worker(broken))


class Hook(TempDir):
    def setUp(self):
        super().setUp()
        self.worker = write_transcript(self.dir, user_message([{"type": "text", "text": WORKER_TEXT}]))
        self.coordinator = os.path.join(self.dir, "coordinator.jsonl")
        with open(self.coordinator, "w", encoding="utf-8") as file:
            file.write(json.dumps(user_message("/orchestration")) + "\n")
        self.repo = os.path.join(self.dir, "repo")
        os.mkdir(self.repo)
        self.git("init", "-q")
        self.git("-c", "user.name=t", "-c", "user.email=t@example.com", "-c", "commit.gpgsign=false",
                 "commit", "-q", "--allow-empty", "-m", "first")

    def git(self, *args):
        return subprocess.run(["git", "-C", self.repo, *args], capture_output=True, text=True, check=True).stdout.strip()

    def write_stamp(self, value):
        path = os.path.join(self.repo, self.git("rev-parse", "--git-path", guard.STAMP_NAME))
        with open(path, "w", encoding="utf-8") as file:
            file.write(value + "\n")

    def run_hook(self, command, transcript=None, cwd=None):
        payload = {"tool_name": "PowerShell", "tool_input": {"command": command}, "cwd": cwd or self.repo}
        if transcript:
            payload["transcript_path"] = transcript
        return subprocess.run([sys.executable, SCRIPT, "--hook"], input=json.dumps(payload).encode("utf-8"),
                              capture_output=True)

    def test_worker_cannot_set_status_but_can_post(self):
        result = self.run_hook("./CodexReview.ps1 -Pr 1 -Post -SetStatus", self.worker)
        self.assertEqual(result.returncode, 2)
        self.assertIn("#195", result.stderr.decode("utf-8"))
        self.assertEqual(self.run_hook("./CodexReview.ps1 -Pr 1 -Post", self.worker).returncode, 0)

    def test_coordinator_and_unknown_can_set_status(self):
        self.assertEqual(self.run_hook("./CodexReview.ps1 -Pr 1 -Post -SetStatus", self.coordinator).returncode, 0)
        self.assertEqual(self.run_hook("./CodexReview.ps1 -Pr 1 -Post -SetStatus").returncode, 0)

    def test_pr_create_needs_a_stamp_of_head(self):
        command = "gh pr create --base develop --body-file b.md"
        result = self.run_hook(command)
        self.assertEqual(result.returncode, 2, "no stamp")
        message = result.stderr.decode("utf-8")
        self.assertIn("./PrePr.ps1", message)
        self.assertIn("自己点検", message)
        self.write_stamp("0" * 40)
        self.assertEqual(self.run_hook(command).returncode, 2, "stale stamp")
        self.write_stamp(self.git("rev-parse", "HEAD"))
        self.assertEqual(self.run_hook(command).returncode, 0, "current stamp")

    def test_stamp_lives_in_the_worktree_git_dir(self):
        # 別の worktree のスタンプは使い回さない
        self.write_stamp(self.git("rev-parse", "HEAD"))
        other = os.path.join(self.dir, "other")
        self.git("worktree", "add", "-q", "--detach", other)
        self.assertEqual(self.run_hook("gh pr create", cwd=other).returncode, 2)

    def test_pr_create_outside_a_repository_is_not_blocked(self):
        outside = os.path.join(self.dir, "outside")
        os.mkdir(outside)
        self.assertEqual(self.run_hook("gh pr create", cwd=outside).returncode, 0)

    def test_other_commands_and_broken_input_pass_silently(self):
        result = self.run_hook("gh pr checks 1")
        self.assertEqual((result.returncode, result.stderr), (0, b""))
        result = subprocess.run([sys.executable, SCRIPT, "--hook"], input=b"not json", capture_output=True)
        self.assertEqual(result.returncode, 0)


if __name__ == "__main__":
    unittest.main()
