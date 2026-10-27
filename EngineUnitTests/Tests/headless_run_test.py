# ヘッドレス実行(EngineUnitTests.exe --game-bin, Issue #11)と、game.binの読み込み(Issue #107/#112/#113)の回帰テスト
#
# テスト用のgame.binを%TEMP%の一時フォルダに書き、EngineUnitTests.exeを別プロセスで起動して、
# 終了コードと --json の出力を確かめる。ウィンドウもGPUも使わない。一時フォルダは最後に削除する。
#   python EngineUnitTests/Tests/headless_run_test.py                   # x64/Debug/EngineUnitTests.exe
#   python EngineUnitTests/Tests/headless_run_test.py --config Release
#   python EngineUnitTests/Tests/headless_run_test.py --exe <EngineUnitTests.exeのパス>
# すべて合格すれば終了コード0、不合格があれば1。
#
# 確かめること:
#   1. スクリプト付きのエンティティを含むgame.binを --frames N で回すと、終了コード0で、スクリプトのupdate()が
#      N回呼ばれる(headless_test_mover は update() ごとに x を 1、y を dt だけ進めるので、最後の位置で分かる)。
#   2. エンティティが0個のgame.binは空のシーンとして終了コード0になる(#107)。256個以上のエンティティも読める(#112)。
#   3. 異常系(ファイルが無い・空・壊れている・未知のコンポーネント・未登録のスクリプト・引数の誤り)では、
#      決められた0以外の終了コードと --json のエラーが返る。assertのダイアログやabort(終了コード3)、クラッシュにならない。
#   4. --json では標準出力がJSONだけになり、ログは標準エラーに出る。パスに日本語が入っていても動く。
#
# NOTE: EngineUnitTests.exe は --interactive を付けない限り、assertを標準エラーに出してabort(終了コード3)するので、
#       Debugでもダイアログで止まらない。念のためタイムアウトも付ける。
import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
TIMEOUT_SECONDS = 60

# 終了コード(DXForgeEngine/Core/Headless.h の exit_code)
EXIT_SUCCESS = 0
EXIT_USAGE_ERROR = 2
EXIT_ABORT = 3
EXIT_FILE_NOT_FOUND = 10
EXIT_READ_FAILED = 11
EXIT_INVALID_FORMAT = 12
EXIT_UNKNOWN_COMPONENT = 13
EXIT_SCRIPT_NOT_FOUND = 14

MOVER_SCRIPT = "headless_test_mover"   # EngineUnitTests/HeadlessTestScripts.cpp で登録している


# ---- game.bin の組み立て(エディタの Project.SaveToBinary と同じ形式。値はすべてリトルエンディアン) ----
def u32(value):
    return struct.pack("<I", value)


def entity(*components):
    return u32(0) + u32(len(components)) + b"".join(components)


def transform(position=(0.0, 0.0, 0.0), rotation=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0)):
    return u32(0) + struct.pack("<9f", *position, *rotation, *scale)


def script(name):
    data = name.encode("utf-8")
    return u32(1) + u32(len(data)) + data


def game_bin(*entities):
    return u32(len(entities)) + b"".join(entities)


# ---- 実行と検証 ----
class Runner:
    def __init__(self, exe, work_dir):
        self.exe = exe
        self.work_dir = work_dir
        self.failures = []
        self.passed = 0
        self._file_index = 0

    def write(self, data, name=None, sub_dir=None):
        self._file_index += 1
        directory = os.path.join(self.work_dir, sub_dir) if sub_dir else self.work_dir
        os.makedirs(directory, exist_ok=True)
        path = os.path.join(directory, name or f"game_{self._file_index}.bin")
        with open(path, "wb") as f:
            f.write(data)
        return path

    def run(self, args):
        proc = subprocess.run([self.exe] + args, capture_output=True, timeout=TIMEOUT_SECONDS)
        stdout = proc.stdout.decode("utf-8", errors="replace")
        stderr = proc.stderr.decode("utf-8", errors="replace")
        return proc.returncode, stdout, stderr

    def run_json(self, args):
        code, stdout, stderr = self.run(args + ["--json"])
        try:
            result = json.loads(stdout)
        except json.JSONDecodeError as e:
            result = None
            stderr += f"\n(stdout is not JSON: {e}: {stdout!r})"
        return code, result, stderr

    def check(self, name, condition, detail=""):
        if condition:
            self.passed += 1
        else:
            self.failures.append(f"{name}: {detail}")
            print(f"[FAIL] {name}: {detail}")

    def check_no_crash(self, name, code, stderr):
        self.check(f"{name}: no assert/abort", code != EXIT_ABORT and "Assertion failed" not in stderr
                   and code >= 0, f"exit code {code}, stderr: {stderr.strip()}")

    def expect_error(self, name, args, expected_code, expected_error):
        code, result, stderr = self.run_json(args)
        self.check_no_crash(name, code, stderr)
        self.check(f"{name}: exit code", code == expected_code, f"expected {expected_code}, got {code}; stderr: {stderr.strip()}")
        if result is None:
            self.check(f"{name}: json", False, stderr)
            return
        self.check(f"{name}: success=false", result.get("success") is False, result)
        self.check(f"{name}: exit_code in json", result.get("exit_code") == expected_code, result)
        error = result.get("error") or {}
        self.check(f"{name}: error code", error.get("code") == expected_error, result)
        self.check(f"{name}: error message", bool(error.get("message")), result)
        self.check(f"{name}: frames_run", result.get("frames_run") == 0, result)
        print(f"[PASS] {name}" if not any(f.startswith(name) for f in self.failures) else f"[----] {name}")


def test_runs_scripts_for_n_frames(r):
    name = "runs scripts for N frames"
    path = r.write(game_bin(
        entity(transform(position=(0.5, 0.0, -1.0)), script(MOVER_SCRIPT)),
        entity(transform(position=(7.0, 8.0, 9.0), scale=(2.0, 3.0, 4.0))),
    ))
    code, result, stderr = r.run_json(["--game-bin", path, "--frames", "10", "--dt", "0.25"])
    r.check_no_crash(name, code, stderr)
    r.check(f"{name}: exit code", code == EXIT_SUCCESS, f"got {code}; stderr: {stderr.strip()}")
    if result is None:
        r.check(f"{name}: json", False, stderr)
        return
    r.check(f"{name}: success", result["success"] is True and result["error"] is None, result)
    r.check(f"{name}: frames", result["frames_requested"] == 10 and result["frames_run"] == 10, result)
    r.check(f"{name}: dt", result["dt"] == 0.25, result)
    r.check(f"{name}: game_bin", os.path.normcase(result["game_bin"]) == os.path.normcase(path), result)
    r.check(f"{name}: entity_count", result["entity_count"] == 2 and len(result["entities"]) == 2, result)
    if len(result["entities"]) == 2:
        mover, static = result["entities"]
        # update()が10回呼ばれた: x = 0.5 + 10、y = 0.25 * 10
        r.check(f"{name}: script updated N times", mover["position"] == [10.5, 2.5, -1.0], mover)
        r.check(f"{name}: entity without script is unchanged",
                static["position"] == [7.0, 8.0, 9.0] and static["scale"] == [2.0, 3.0, 4.0]
                and static["rotation"] == [0.0, 0.0, 0.0, 1.0], static)
    r.check(f"{name}: logs go to stderr", "[headless] loaded 2 entities" in stderr, stderr)
    print(f"[PASS] {name}" if not any(f.startswith(name) for f in r.failures) else f"[----] {name}")


def test_zero_frames(r):
    name = "zero frames"
    path = r.write(game_bin(entity(transform(position=(1.0, 2.0, 3.0)), script(MOVER_SCRIPT))))
    code, result, stderr = r.run_json(["--game-bin", path, "--frames", "0"])
    r.check_no_crash(name, code, stderr)
    r.check(f"{name}: exit code", code == EXIT_SUCCESS, f"got {code}; stderr: {stderr.strip()}")
    if result:
        r.check(f"{name}: no update", result["frames_run"] == 0 and result["entities"][0]["position"] == [1.0, 2.0, 3.0], result)
    print(f"[PASS] {name}" if not any(f.startswith(name) for f in r.failures) else f"[----] {name}")


def test_zero_entities_is_empty_scene(r):
    name = "zero entities is an empty scene (#107)"
    path = r.write(game_bin())
    code, result, stderr = r.run_json(["--game-bin", path, "--frames", "5"])
    r.check_no_crash(name, code, stderr)
    r.check(f"{name}: exit code", code == EXIT_SUCCESS, f"got {code}; stderr: {stderr.strip()}")
    if result:
        r.check(f"{name}: result", result["success"] is True and result["frames_run"] == 5
                and result["entity_count"] == 0 and result["entities"] == [], result)
    print(f"[PASS] {name}" if not any(f.startswith(name) for f in r.failures) else f"[----] {name}")


def test_many_entities(r):
    name = "300 entities (#112)"
    count = 300
    path = r.write(game_bin(*[entity(transform(position=(float(i), 0.0, 0.0)), script(MOVER_SCRIPT)) for i in range(count)]))
    code, result, stderr = r.run_json(["--game-bin", path, "--frames", "3", "--dt", "0.5"])
    r.check_no_crash(name, code, stderr)
    r.check(f"{name}: exit code", code == EXIT_SUCCESS, f"got {code}; stderr: {stderr.strip()}")
    if result:
        r.check(f"{name}: entity_count", result["entity_count"] == count, result["entity_count"])
        r.check(f"{name}: all updated", all(e["position"] == [float(i) + 3.0, 1.5, 0.0] for i, e in enumerate(result["entities"])),
                "positions differ")
    print(f"[PASS] {name}" if not any(f.startswith(name) for f in r.failures) else f"[----] {name}")


def test_human_readable_output(r):
    name = "human readable output"
    path = r.write(game_bin(entity(transform(), script(MOVER_SCRIPT))))
    code, stdout, stderr = r.run(["--game-bin", path, "--frames", "2"])
    r.check_no_crash(name, code, stderr)
    r.check(f"{name}: exit code", code == EXIT_SUCCESS, f"got {code}")
    r.check(f"{name}: summary", "headless: OK, 2 frames, 1 entities" in stdout, stdout)
    print(f"[PASS] {name}" if not any(f.startswith(name) for f in r.failures) else f"[----] {name}")


def test_unicode_path(r):
    name = "path with Japanese characters"
    path = r.write(game_bin(entity(transform(), script(MOVER_SCRIPT))), name="ゲーム.bin", sub_dir="日本語のフォルダ")
    code, result, stderr = r.run_json(["--game-bin", path, "--frames", "1"])
    r.check_no_crash(name, code, stderr)
    r.check(f"{name}: exit code", code == EXIT_SUCCESS, f"got {code}; stderr: {stderr.strip()}")
    if result:
        r.check(f"{name}: game_bin", os.path.normcase(result["game_bin"]) == os.path.normcase(path), result["game_bin"])
    print(f"[PASS] {name}" if not any(f.startswith(name) for f in r.failures) else f"[----] {name}")


def test_errors(r):
    valid_entity = entity(transform(), script(MOVER_SCRIPT))
    r.expect_error("missing game.bin", ["--game-bin", os.path.join(r.work_dir, "no_such.bin"), "--frames", "1"],
                   EXIT_FILE_NOT_FOUND, "file_not_found")
    r.expect_error("empty game.bin", ["--game-bin", r.write(b""), "--frames", "1"],
                   EXIT_READ_FAILED, "read_failed")
    r.expect_error("truncated game.bin", ["--game-bin", r.write(game_bin(valid_entity)[:-3]), "--frames", "1"],
                   EXIT_INVALID_FORMAT, "invalid_format")
    r.expect_error("fewer entities than the count", ["--game-bin", r.write(u32(2) + valid_entity), "--frames", "1"],
                   EXIT_INVALID_FORMAT, "invalid_format")
    r.expect_error("trailing bytes", ["--game-bin", r.write(game_bin(valid_entity) + b"\x00"), "--frames", "1"],
                   EXIT_INVALID_FORMAT, "invalid_format")
    r.expect_error("script name length of 256 (#112)",
                   ["--game-bin", r.write(game_bin(entity(transform(), u32(1) + u32(256) + b"a" * 256))), "--frames", "1"],
                   EXIT_INVALID_FORMAT, "invalid_format")
    r.expect_error("unknown component", ["--game-bin", r.write(game_bin(entity(transform(), u32(9)))), "--frames", "1"],
                   EXIT_UNKNOWN_COMPONENT, "unknown_component")
    r.expect_error("unregistered script (#113)",
                   ["--game-bin", r.write(game_bin(valid_entity, entity(transform(), script("no_such_script")))), "--frames", "1"],
                   EXIT_SCRIPT_NOT_FOUND, "script_not_found")
    path = r.write(game_bin(valid_entity))
    r.expect_error("missing --frames", ["--game-bin", path], EXIT_USAGE_ERROR, "usage_error")
    r.expect_error("invalid --frames", ["--game-bin", path, "--frames", "-1"], EXIT_USAGE_ERROR, "usage_error")
    r.expect_error("invalid --dt", ["--game-bin", path, "--frames", "1", "--dt", "abc"], EXIT_USAGE_ERROR, "usage_error")
    r.expect_error("unknown argument", ["--game-bin", path, "--frames", "1", "--bogus"], EXIT_USAGE_ERROR, "usage_error")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", default="Debug", choices=["Debug", "Release"])
    parser.add_argument("--exe", help="EngineUnitTests.exe のパス(省略時は x64/<config>/EngineUnitTests.exe)")
    args = parser.parse_args()
    exe = args.exe or os.path.join(REPO_ROOT, "x64", args.config, "EngineUnitTests.exe")
    if not os.path.isfile(exe):
        print(f"EngineUnitTests.exe not found: {exe}", file=sys.stderr)
        return 1

    work_dir = tempfile.mkdtemp(prefix="dxforge_headless_test_")
    try:
        r = Runner(exe, work_dir)
        test_runs_scripts_for_n_frames(r)
        test_zero_frames(r)
        test_zero_entities_is_empty_scene(r)
        test_many_entities(r)
        test_human_readable_output(r)
        test_unicode_path(r)
        test_errors(r)
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)

    print("----")
    print(f"{r.passed} checks passed, {len(r.failures)} failed ({exe})")
    return 1 if r.failures else 0


if __name__ == "__main__":
    sys.exit(main())
