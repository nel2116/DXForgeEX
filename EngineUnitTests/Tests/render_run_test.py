# 描画のヘッドレス実行(EngineUnitTests.exe --render, Issue #127)の回帰テスト
#
# EngineUnitTests.exe を別プロセスで起動し、終了コード・--json の出力・書き出したPNGを確かめる。ウィンドウは作らない。
# PNGや一時的にコピーした実行ファイルは %TEMP% の一時フォルダに置き、最後に削除する。
#   python EngineUnitTests/Tests/render_run_test.py                   # x64/Debug/EngineUnitTests.exe
#   python EngineUnitTests/Tests/render_run_test.py --config Release
#   python EngineUnitTests/Tests/render_run_test.py --exe <EngineUnitTests.exeのパス>
#   python EngineUnitTests/Tests/render_run_test.py --update-baselines --config Release   # 基準画像を更新する(テストは実行しない)
# すべて合格すれば終了コード0、不合格があれば1。
#
# 確かめること:
#   1. --render --adapter warp で、終了コード0でPNG(320x240)が書き出され、テストシーンが写っている
#      (中央に赤い球、左上に背景、下に床)。--json の出力とログ(標準エラー)の分け方。
#   2. 同じ引数で2回実行すると、WARPではピクセル(--json の pixel_hash とデコードした画素)とPNGのファイルが一致する。
#   3. --adapter hardware(既定のアダプタの選び方)でも動く。
#   4. 引数の誤りは終了コード2、PNGを書けないときは23、DXC(dxcompiler.dll)やシェーダーのソースが見つからないときは21、
#      グラフィックスの初期化に失敗したとき(shaders.binが空)は20、シェーダーのコンパイル中に例外が出たとき
#      (実行ファイルの隣に shaders という通常のファイルがある)は21になる。
#      assertのダイアログやabort(終了コード3)、クラッシュにならない。
#   5. WARPで描いた画素が、構成ごとの基準画像(baselines/render_warp_320x240_<構成>.png)と一致する(段階5〜6)。
#      チャンネルごとの差が BASELINE_TOLERANCE 階調を超えた画素が1つでもあれば失敗にし、
#      %TEMP%\dxforge_render_diff\<構成>\(環境変数 DXFORGE_RENDER_DIFF_DIR で変更できる。CIで使う)に
#      expected.png / actual.png / diff.png(基準|実際|差分を横に並べた画像)を書く。合格したときは何も残さない。
#      --adapter hardware の結果は基準画像と比べない(GPUやドライバーで変わるため)。差を表示するだけにする。
#   構成は --config か、--exe のフォルダ名(x64/<構成>/、build.ps1 -RunTests の渡し方)から決める。
#
# DXC(dxcompiler.dll)が実行ファイルの隣にないとき(packages/DirectXShaderCompiler なしで EngineUnitTests だけをビルドした場合、#61)は、
# 描画が要るテストをスキップする(理由を表示する)。環境変数 DXFORGE_REQUIRE_RENDER_TEST=1 のとき(CI)は、スキップせずに失敗にする。
import argparse
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
# NOTE: Debugビルドでは、シェーダーのコンパイル(最適化なし)とWARPでの描画に時間がかかる
TIMEOUT_SECONDS = 300
WIDTH = 320
HEIGHT = 240
BASELINE_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "baselines")
# NOTE: WARPでは同じ実行ファイルならビット単位で一致するが、ランナーのイメージの更新などによる
#       1階調の揺れでは落とさない。1階調の差の画素の数は表示する
BASELINE_TOLERANCE = 1

# 終了コード(DXForgeEngine/Core/Headless.h の exit_code)
EXIT_SUCCESS = 0
EXIT_USAGE_ERROR = 2
EXIT_ABORT = 3
EXIT_GRAPHICS_INIT_FAILED = 20
EXIT_SHADERS_UNAVAILABLE = 21
EXIT_SCREENSHOT_WRITE_FAILED = 23


# ---- PNGの読み込み(標準ライブラリだけで、8ビットのRGB/RGBA・インターレースなしを読む) ----
def read_png(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG file")
    pos = 8
    width = height = None
    channels = None
    compressed = b""
    while pos < len(data):
        length, chunk_type = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if bit_depth != 8 or color_type not in (2, 6) or interlace != 0:
                raise ValueError(f"unsupported PNG (bit depth {bit_depth}, color type {color_type}, interlace {interlace})")
            channels = 3 if color_type == 2 else 4
        elif chunk_type == b"IDAT":
            compressed += body
        elif chunk_type == b"IEND":
            break
    raw = zlib.decompress(compressed)
    stride = width * channels
    pixels = bytearray()
    previous = bytearray(stride)
    offset = 0
    for _ in range(height):
        filter_type = raw[offset]
        line = bytearray(raw[offset + 1:offset + 1 + stride])
        offset += 1 + stride
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = previous[i]
            c = previous[i - channels] if i >= channels else 0
            if filter_type == 1:
                line[i] = (line[i] + a) & 0xff
            elif filter_type == 2:
                line[i] = (line[i] + b) & 0xff
            elif filter_type == 3:
                line[i] = (line[i] + (a + b) // 2) & 0xff
            elif filter_type == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                predictor = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + predictor) & 0xff
        pixels += line
        previous = line
    # RGBに揃える
    if channels == 4:
        pixels = bytearray(b for i, b in enumerate(pixels) if i % 4 != 3)
    return width, height, bytes(pixels)


def write_png(path, width, height, rgb):
    """8ビットのRGB(フィルタなし、インターレースなし)でPNGを書く"""
    def chunk(chunk_type, body):
        return struct.pack(">I", len(body)) + chunk_type + body + struct.pack(">I", zlib.crc32(chunk_type + body))
    stride = width * 3
    raw = b"".join(b"\x00" + rgb[y * stride:(y + 1) * stride] for y in range(height))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def compare_pixels(expected, actual, width, tolerance):
    """RGB8の画素を比べる。チャンネルごとの差の最大が tolerance を超えた画素を数える

    @return dict(over: 超えた画素の数, within: 差が1..toleranceの画素の数, max_diff: 最大の差,
                 bbox: 超えた画素の範囲 (x0, y0, x1, y1) かNone, mask: 画素ごとの最大の差のbytes)
    """
    over = within = max_diff = 0
    x0 = y0 = x1 = y1 = None
    mask = bytearray(len(expected) // 3)
    for i in range(len(mask)):
        j = i * 3
        d = max(abs(expected[j] - actual[j]), abs(expected[j + 1] - actual[j + 1]), abs(expected[j + 2] - actual[j + 2]))
        if d == 0:
            continue
        mask[i] = d
        max_diff = max(max_diff, d)
        if d <= tolerance:
            within += 1
            continue
        over += 1
        x, y = i % width, i // width
        x0 = x if x0 is None else min(x0, x)
        y0 = y if y0 is None else min(y0, y)
        x1 = x if x1 is None else max(x1, x)
        y1 = y if y1 is None else max(y1, y)
    return {"over": over, "within": within, "max_diff": max_diff,
            "bbox": None if x0 is None else (x0, y0, x1, y1), "mask": bytes(mask)}


def make_diff_image(expected, actual, mask, width, height, tolerance):
    """基準|実際|差分 を横に並べたRGB8の画素を作る。差分は基準を暗いグレーにし、
    許容値を超えた画素を赤、許容値以内の差を黄で塗る"""
    diff = bytearray()
    for i in range(width * height):
        j = i * 3
        if mask[i] > tolerance:
            diff += b"\xff\x00\x00"
        elif mask[i] > 0:
            diff += b"\xff\xff\x00"
        else:
            gray = (expected[j] + expected[j + 1] + expected[j + 2]) // 12
            diff += bytes((gray, gray, gray))
    stride = width * 3
    out = bytearray()
    for y in range(height):
        row = slice(y * stride, (y + 1) * stride)
        out += expected[row] + actual[row] + diff[row]
    return bytes(out)


def baseline_path(config):
    return os.path.join(BASELINE_DIR, f"render_warp_{WIDTH}x{HEIGHT}_{config}.png")


def diff_dir(config):
    return os.environ.get("DXFORGE_RENDER_DIFF_DIR") or os.path.join(tempfile.gettempdir(), "dxforge_render_diff", config)


def pixel(rgb, width, x, y):
    i = (y * width + x) * 3
    return rgb[i], rgb[i + 1], rgb[i + 2]


def fnv1a64(data):
    h = 0xcbf29ce484222325
    for byte in data:
        h ^= byte
        h = (h * 0x100000001b3) & 0xffffffffffffffff
    return f"{h:016x}"


# ---- 実行と検証 ----
class Runner:
    def __init__(self, exe, work_dir):
        self.exe = exe
        self.work_dir = work_dir
        self.failures = []
        self.passed = 0
        self.skipped = []

    def run_raw(self, args, exe=None):
        proc = subprocess.run([exe or self.exe] + args, capture_output=True, timeout=TIMEOUT_SECONDS)
        return proc.returncode, proc.stdout, proc.stderr.decode("utf-8", errors="replace")

    def run(self, args):
        code, stdout, stderr = self.run_raw(args)
        return code, stdout.decode("utf-8", errors="replace"), stderr

    def run_json(self, args, exe=None):
        code, stdout, stderr = self.run_raw(args + ["--json"], exe)
        try:
            result = json.loads(stdout.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as e:
            result = None
            stderr += f"\n(stdout is not UTF-8 JSON: {e}: {stdout!r})"
        return code, result, stderr

    def check(self, name, condition, detail=""):
        if condition:
            self.passed += 1
        else:
            self.failures.append(f"{name}: {detail}")
            print(f"[FAIL] {name}: {detail}")

    def check_no_crash(self, name, code, stderr):
        # NOTE: アクセス違反などのクラッシュは 0xC0000005 のような大きな終了コードになる
        self.check(f"{name}: no assert/abort/crash", code != EXIT_ABORT and "Assertion failed" not in stderr
                   and 0 <= code < 256, f"exit code {code}, stderr: {stderr.strip()}")

    def report(self, name):
        print(f"[PASS] {name}" if not any(f.startswith(name) for f in self.failures) else f"[----] {name}")

    def render_args(self, png, adapter="warp", frames=3):
        return ["--render", "--frames", str(frames), "--width", str(WIDTH), "--height", str(HEIGHT),
                "--screenshot", png, "--adapter", adapter]

    def expect_error(self, name, args, expected_code, expected_error, exe=None, message_part=None, rendered=False):
        code, result, stderr = self.run_json(args, exe)
        self.check_no_crash(name, code, stderr)
        self.check(f"{name}: exit code", code == expected_code, f"expected {expected_code}, got {code}; stderr: {stderr.strip()}")
        if result is None:
            self.check(f"{name}: json", False, stderr)
            return
        self.check(f"{name}: success=false", result.get("success") is False and result.get("exit_code") == expected_code, result)
        error = result.get("error") or {}
        self.check(f"{name}: error code", error.get("code") == expected_error, result)
        self.check(f"{name}: error message", bool(error.get("message")) and (message_part is None or message_part in error["message"]), result)
        # 描画のあとで失敗したとき(PNGを書けない)は、描画した画素のハッシュが返る
        self.check(f"{name}: pixel hash", (result.get("pixel_hash") is not None) == rendered, result)
        self.report(name)


def render_ok(r, name, png, adapter="warp"):
    """描画して、成功したことと --json の内容を確かめる。結果(JSON)を返す(失敗ならNone)"""
    code, result, stderr = r.run_json(r.render_args(png, adapter))
    r.check_no_crash(name, code, stderr)
    r.check(f"{name}: exit code", code == EXIT_SUCCESS, f"got {code}; stderr: {stderr.strip()}")
    if result is None:
        r.check(f"{name}: json", False, stderr)
        return None
    r.check(f"{name}: success", result.get("success") is True and result.get("error") is None, result)
    r.check(f"{name}: frames", result.get("frames_requested") == 3 and result.get("frames_run") == 3, result)
    r.check(f"{name}: size", result.get("width") == WIDTH and result.get("height") == HEIGHT, result)
    r.check(f"{name}: adapter", result.get("adapter") == adapter and bool(result.get("adapter_name")), result)
    r.check(f"{name}: screenshot", os.path.normcase(result.get("screenshot", "")) == os.path.normcase(os.path.abspath(png)), result)
    r.check(f"{name}: logs go to stderr", "[render] adapter: " in stderr and "[render] wrote " in stderr, stderr)
    print(f"  {name}: adapter_name={result.get('adapter_name')!r} pixel_hash={result.get('pixel_hash')}")
    return result if code == EXIT_SUCCESS else None


def test_warp_render_and_determinism(r):
    name = "warp render"
    first = os.path.join(r.work_dir, "warp_1.png")
    result = render_ok(r, name, first)
    if result is None:
        return
    try:
        width, height, rgb = read_png(first)
    except (OSError, ValueError, zlib.error) as e:
        r.check(f"{name}: png", False, repr(e))
        return
    r.check(f"{name}: png size", (width, height) == (WIDTH, HEIGHT), (width, height))
    r.check(f"{name}: pixel_hash matches the PNG", result.get("pixel_hash") == fnv1a64(rgb), result.get("pixel_hash"))
    # テストシーン(HeadlessRender.cpp): 中央に赤い球、左上は背景(何も描かれない)、下の中央は灰色の床
    # NOTE: 背景はGパスのクリア色で、Debugでは灰色(0.5)、Releaseでは黒(D3D12GPass.cppのclear_value)
    sphere = pixel(rgb, width, width // 2, height // 2 - 20)
    background = pixel(rgb, width, 5, 5)
    floor = pixel(rgb, width, width // 2, height - 10)
    r.check(f"{name}: sphere is red", sphere[0] > sphere[1] + 40 and sphere[0] > sphere[2] + 40, sphere)
    r.check(f"{name}: background is the clear color", background in ((0, 0, 0), (188, 188, 188)), background)
    r.check(f"{name}: floor is lit", min(floor) > 80, floor)
    r.report(name)

    name = "warp render is deterministic"
    second = os.path.join(r.work_dir, "warp_2.png")
    again = render_ok(r, name, second)
    if again is None:
        return rgb
    r.check(f"{name}: pixel_hash", again.get("pixel_hash") == result.get("pixel_hash"), (result.get("pixel_hash"), again.get("pixel_hash")))
    _, _, rgb_again = read_png(second)
    r.check(f"{name}: pixels", rgb_again == rgb, "decoded pixels differ")
    with open(first, "rb") as a, open(second, "rb") as b:
        r.check(f"{name}: png file", hashlib.sha256(a.read()).digest() == hashlib.sha256(b.read()).digest(), "PNG files differ")
    r.report(name)
    return rgb


def read_baseline(config):
    """基準画像を読む。@return (RGB8の画素, エラーの説明) のどちらかがNone"""
    path = baseline_path(config)
    hint = f"run: python EngineUnitTests/Tests/render_run_test.py --update-baselines --config {config}"
    try:
        width, height, rgb = read_png(path)
    except FileNotFoundError:
        return None, f"baseline not found: {path} ({hint})"
    except (OSError, ValueError, zlib.error) as e:
        return None, f"cannot read {path}: {e!r}"
    if (width, height) != (WIDTH, HEIGHT):
        return None, f"{path} is {width}x{height}, expected {WIDTH}x{HEIGHT} ({hint})"
    return rgb, None


def test_baseline_comparison(r, rgb, config, require_render):
    """WARPで描いた画素を、構成ごとの基準画像と比べる。違えば差分の画像を diff_dir(config) に書く"""
    name = "warp render matches the baseline"
    if config is None:
        reason = (f"cannot tell the configuration from {r.exe} (expected x64/Debug or x64/Release); "
                  "pass --config Debug|Release")
        if require_render:
            r.check(name, False, f"DXFORGE_REQUIRE_RENDER_TEST=1 but {reason}")
        else:
            r.skipped.append(reason)
            print(f"[SKIP] {name}: {reason}")
        return
    out_dir = diff_dir(config)
    # 前回の失敗の差分を残さない(CIで古い差分を成果物にしないため)
    shutil.rmtree(out_dir, ignore_errors=True)
    expected, error = read_baseline(config)
    if expected is None:
        r.check(f"{name}: baseline", False, error)
        return
    if len(rgb) != len(expected):
        r.check(f"{name}: size", False, f"rendered {len(rgb) // 3} pixels, baseline has {len(expected) // 3}")
        return
    result = compare_pixels(expected, rgb, WIDTH, BASELINE_TOLERANCE)
    print(f"  {name} ({os.path.basename(baseline_path(config))}): {result['over']} pixels over {BASELINE_TOLERANCE} level(s), "
          f"{result['within']} pixels within, max diff {result['max_diff']}")
    if result["over"] > 0:
        os.makedirs(out_dir, exist_ok=True)
        write_png(os.path.join(out_dir, "expected.png"), WIDTH, HEIGHT, expected)
        write_png(os.path.join(out_dir, "actual.png"), WIDTH, HEIGHT, rgb)
        write_png(os.path.join(out_dir, "diff.png"), WIDTH * 3, HEIGHT,
                  make_diff_image(expected, rgb, result["mask"], WIDTH, HEIGHT, BASELINE_TOLERANCE))
    r.check(f"{name}: pixels", result["over"] == 0,
            f"{result['over']} of {WIDTH * HEIGHT} pixels differ by more than {BASELINE_TOLERANCE} level(s) "
            f"(max diff {result['max_diff']}, bbox {result['bbox']}); expected|actual|diff written to {out_dir}")
    r.report(name)


def test_hardware_render(r, config):
    name = "hardware render"
    png = os.path.join(r.work_dir, "hardware.png")
    result = render_ok(r, name, png, adapter="hardware")
    if result is not None:
        width, height, rgb = read_png(png)
        r.check(f"{name}: png size", (width, height) == (WIDTH, HEIGHT), (width, height))
        r.check(f"{name}: pixel_hash matches the PNG", result.get("pixel_hash") == fnv1a64(rgb), result.get("pixel_hash"))
        # GPUやドライバーで画素が変わるので、基準画像(WARP)とは比べずに差を表示するだけにする
        expected = read_baseline(config)[0] if config else None
        if expected is not None and len(expected) == len(rgb):
            diff = compare_pixels(expected, rgb, WIDTH, 0)
            print(f"  {name} vs the WARP baseline (not checked): {diff['over']} pixels differ, max diff {diff['max_diff']}")
    r.report(name)


def test_human_readable_and_unicode_path(r):
    name = "human readable output, path with Japanese characters"
    directory = os.path.join(r.work_dir, "日本語のフォルダ")
    os.makedirs(directory, exist_ok=True)
    png = os.path.join(directory, "画像.png")
    code, stdout, stderr = r.run(r.render_args(png))
    r.check_no_crash(name, code, stderr)
    r.check(f"{name}: exit code", code == EXIT_SUCCESS, f"got {code}; stderr: {stderr.strip()}")
    r.check(f"{name}: summary", "render: OK, 3 frames, 320x240, warp" in stdout, stdout)
    r.check(f"{name}: png written", os.path.isfile(png), png)
    r.report(name)


def test_screenshot_errors(r):
    r.expect_error("screenshot folder does not exist",
                   r.render_args(os.path.join(r.work_dir, "no_such_folder", "a.png")),
                   EXIT_SCREENSHOT_WRITE_FAILED, "screenshot_write_failed", rendered=True)
    r.check("screenshot folder does not exist: nothing written",
            not os.path.exists(os.path.join(r.work_dir, "no_such_folder")), "folder was created")


def test_usage_errors(r):
    png = os.path.join(r.work_dir, "usage.png")
    base = ["--render", "--frames", "1", "--width", "16", "--height", "16", "--screenshot", png]
    r.expect_error("missing --screenshot", base[:-2], EXIT_USAGE_ERROR, "usage_error", message_part="--screenshot")
    r.expect_error("missing --frames", ["--render", "--width", "16", "--height", "16", "--screenshot", png],
                   EXIT_USAGE_ERROR, "usage_error", message_part="--frames")
    r.expect_error("missing --width", ["--render", "--frames", "1", "--height", "16", "--screenshot", png],
                   EXIT_USAGE_ERROR, "usage_error", message_part="--width")
    r.expect_error("zero frames", base[:2] + ["0"] + base[3:], EXIT_USAGE_ERROR, "usage_error")
    r.expect_error("zero width", base[:4] + ["0"] + base[5:], EXIT_USAGE_ERROR, "usage_error")
    r.expect_error("too large height", base[:6] + ["16385"] + base[7:], EXIT_USAGE_ERROR, "usage_error")
    # ライトカリングのタイル(16ピクセル)より小さいと、D3D12LightCulling.cpp の assert に当たる(Codexの指摘、#128)
    r.expect_error("width below the light culling tile size", base[:4] + ["8"] + base[5:], EXIT_USAGE_ERROR, "usage_error",
                   message_part="--width")
    r.expect_error("height below the light culling tile size", base[:6] + ["15"] + base[7:], EXIT_USAGE_ERROR, "usage_error",
                   message_part="--height")
    r.expect_error("invalid --frames", base[:2] + ["abc"] + base[3:], EXIT_USAGE_ERROR, "usage_error")
    r.expect_error("unknown adapter", base + ["--adapter", "gpu"], EXIT_USAGE_ERROR, "usage_error", message_part="--adapter")
    r.expect_error("unknown argument", base + ["--bogus"], EXIT_USAGE_ERROR, "usage_error", message_part="--bogus")
    r.expect_error("--render with --game-bin", base + ["--game-bin", "game.bin"], EXIT_USAGE_ERROR, "usage_error")
    r.check("usage errors: nothing written", not os.path.exists(png), png)

    name = "--help lists --render and exit codes"
    code, stdout, stderr = r.run(["--help"])
    r.check(f"{name}: exit code", code == EXIT_SUCCESS, code)
    r.check(f"{name}: text", all(s in stdout for s in ("--render", "--screenshot", "--adapter", "16..16384", "21 shaders unavailable")), stdout)
    r.report(name)


def test_shaders_unavailable(r, has_dxc):
    # 実行ファイルだけを一時フォルダにコピーすると、DXCが見つからない
    folder = os.path.join(r.work_dir, "exe_without_dxc")
    os.makedirs(folder)
    exe = shutil.copy2(r.exe, folder)
    png = os.path.join(r.work_dir, "no_dxc.png")
    r.expect_error("dxcompiler.dll not found", r.render_args(png), EXIT_SHADERS_UNAVAILABLE, "shaders_unavailable",
                   exe=exe, message_part="dxcompiler.dll")
    if not has_dxc:
        return
    # DXCも一緒にコピーすると、今度はシェーダーのソース(..\..\DXForgeEngine など)が見つからない
    folder = os.path.join(r.work_dir, "exe_without_sources")
    os.makedirs(folder)
    exe = shutil.copy2(r.exe, folder)
    for dll in ("dxcompiler.dll", "dxil.dll"):
        shutil.copy2(os.path.join(os.path.dirname(r.exe), dll), folder)
    r.expect_error("shader sources not found", r.render_args(png), EXIT_SHADERS_UNAVAILABLE, "shaders_unavailable",
                   exe=exe, message_part="shader sources")


def make_repo_copy(r, folder_name):
    """リポジトリと同じ並び(x64/<構成>/ の実行ファイルとDXC、..\\..\\ のシェーダーのソース)を一時フォルダに作る

    @return (実行ファイルのパス, 実行ファイルのフォルダ)
    """
    root = os.path.join(r.work_dir, folder_name)
    exe_folder = os.path.join(root, "x64", "Config")
    os.makedirs(exe_folder)
    exe = shutil.copy2(r.exe, exe_folder)
    for dll in ("dxcompiler.dll", "dxil.dll"):
        shutil.copy2(os.path.join(os.path.dirname(r.exe), dll), exe_folder)
    shutil.copytree(os.path.join(REPO_ROOT, "DXForgeEngine", "Graphics", "Direct3D12", "Shaders"),
                    os.path.join(root, "DXForgeEngine", "Graphics", "Direct3D12", "Shaders"))
    os.makedirs(os.path.join(root, "EngineTest"))
    shutil.copy2(os.path.join(REPO_ROOT, "EngineTest", "TestShader.hlsl"), os.path.join(root, "EngineTest"))
    return exe, exe_folder


def test_graphics_init_failed(r):
    """graphics::initialize()が失敗しても、クラッシュせずに終了コード20を返す(Codexの指摘、#128)

    リポジトリと同じ並び(x64/<構成>/ と ..\\..\\ のシェーダーのソース)を一時フォルダに作り、ソースより新しい空の
    shaders.bin を置く。compile_shaders() は最新と見なしてコンパイルせず、エンジンのシェーダーの読み込み
    (shaders::initialize())が失敗して、D3D12の初期化が failed_init() で終わる。
    """
    name = "graphics initialization fails (empty shaders.bin)"
    exe, exe_folder = make_repo_copy(r, "init_failure_repo")
    shaders_bin = os.path.join(exe_folder, "shaders", "d3d12", "shaders.bin")
    os.makedirs(os.path.dirname(shaders_bin))
    open(shaders_bin, "wb").close()
    # NOTE: copy2 はソースの更新日時を保つので、今作った shaders.bin のほうが新しい
    png = os.path.join(r.work_dir, "init_failure.png")
    r.expect_error(name, r.render_args(png), EXIT_GRAPHICS_INIT_FAILED, "graphics_init_failed", exe=exe,
                   message_part="failed to initialize Direct3D 12")
    r.check(f"{name}: nothing written", not os.path.exists(png), png)


def test_shader_compilation_throws(r):
    """compile_shaders() の中の std::filesystem が例外を投げても、終了コード21とJSONを返す(Codexの指摘、#128)

    実行ファイルの隣に shaders という通常のファイルを置くと、コンパイルしたシェーダーを書き出すときの
    create_directories(".\\shaders\\d3d12") が filesystem_error を投げる。
    """
    name = "shader compilation throws (a file named shaders)"
    exe, exe_folder = make_repo_copy(r, "shaders_file_repo")
    open(os.path.join(exe_folder, "shaders"), "wb").close()
    png = os.path.join(r.work_dir, "shaders_file.png")
    r.expect_error(name, r.render_args(png), EXIT_SHADERS_UNAVAILABLE, "shaders_unavailable", exe=exe,
                   message_part="exception")
    r.check(f"{name}: nothing written", not os.path.exists(png), png)


def update_baselines(exe, args, config, has_dxc):
    """WARPで2回描き、画素が一致したら baselines/ の基準画像を書き換える。@return 終了コード"""
    # 意図しない更新を防ぐ: 構成を明示させ、CIでは更新しない
    if args.config is None and args.exe is None:
        print("--update-baselines needs --config Debug|Release (or --exe x64/<config>/EngineUnitTests.exe)", file=sys.stderr)
        return 2
    if config is None:
        print(f"cannot tell the configuration from {exe}; pass --config Debug|Release", file=sys.stderr)
        return 2
    if os.environ.get("CI"):
        print("--update-baselines is refused in CI (CI is set); update the baselines locally and commit them", file=sys.stderr)
        return 2
    if not has_dxc:
        print(f"dxcompiler.dll/dxil.dll are not next to {exe}; build EngineTest or run Restore.ps1", file=sys.stderr)
        return 1
    work_dir = tempfile.mkdtemp(prefix="dxforge_render_baseline_")
    try:
        r = Runner(exe, work_dir)
        pngs = [os.path.join(work_dir, f"warp_{i}.png") for i in (1, 2)]
        results = [render_ok(r, f"baseline render {i + 1}", png) for i, png in enumerate(pngs)]
        if r.failures or None in results:
            print("rendering failed; baselines were not changed", file=sys.stderr)
            return 1
        rgbs = [read_png(png) for png in pngs]
        if rgbs[0] != rgbs[1] or rgbs[0][:2] != (WIDTH, HEIGHT):
            print("two WARP renders differ (or the size is wrong); baselines were not changed", file=sys.stderr)
            return 1
        path = baseline_path(config)
        os.makedirs(BASELINE_DIR, exist_ok=True)
        shutil.copyfile(pngs[0], path)
        print(f"updated {path} (pixel_hash {results[0].get('pixel_hash')}, adapter {results[0].get('adapter_name')!r}, {exe})")
        return 0
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", choices=["Debug", "Release"],
                        help="構成(省略時は --exe のフォルダ名から決め、--exe もなければ Debug)")
    parser.add_argument("--exe", help="EngineUnitTests.exe のパス(省略時は x64/<config>/EngineUnitTests.exe)")
    parser.add_argument("--update-baselines", action="store_true",
                        help="WARPで描いた画像で baselines/ の基準画像を書き換える(--config か --exe が必要。テストは実行しない)")
    args = parser.parse_args()
    exe = os.path.abspath(args.exe or os.path.join(REPO_ROOT, "x64", args.config or "Debug", "EngineUnitTests.exe"))
    if not os.path.isfile(exe):
        print(f"EngineUnitTests.exe not found: {exe}", file=sys.stderr)
        return 1
    # 基準画像は構成ごとにある。build.ps1 -RunTests は --exe x64/<構成>/EngineUnitTests.exe だけを渡す
    folder = os.path.basename(os.path.dirname(exe))
    config = args.config or (folder if folder in ("Debug", "Release") else None)

    require_render = os.environ.get("DXFORGE_REQUIRE_RENDER_TEST") == "1"
    has_dxc = all(os.path.isfile(os.path.join(os.path.dirname(exe), dll)) for dll in ("dxcompiler.dll", "dxil.dll"))

    if args.update_baselines:
        return update_baselines(exe, args, config, has_dxc)

    work_dir = tempfile.mkdtemp(prefix="dxforge_render_test_")
    try:
        r = Runner(exe, work_dir)
        test_usage_errors(r)
        test_shaders_unavailable(r, has_dxc)
        if has_dxc:
            test_graphics_init_failed(r)
            test_shader_compilation_throws(r)
            rgb = test_warp_render_and_determinism(r)
            if rgb is not None:
                test_baseline_comparison(r, rgb, config, require_render)
            test_hardware_render(r, config)
            test_human_readable_and_unicode_path(r)
            test_screenshot_errors(r)
        else:
            reason = (f"dxcompiler.dll/dxil.dll are not next to {exe} "
                      "(EngineUnitTests was built without packages/DirectXShaderCompiler; build EngineTest or run Restore.ps1)")
            if require_render:
                r.check("render tests", False, f"DXFORGE_REQUIRE_RENDER_TEST=1 but {reason}")
            else:
                r.skipped.append(reason)
                print(f"[SKIP] render tests: {reason}")
    finally:
        shutil.rmtree(work_dir, ignore_errors=True)

    print("----")
    skipped = f", render tests skipped" if r.skipped else ""
    print(f"{r.passed} checks passed, {len(r.failures)} failed{skipped} ({exe})")
    return 1 if r.failures else 0


if __name__ == "__main__":
    sys.exit(main())
