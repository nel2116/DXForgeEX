# ないソースファイルの取り込みが assert で止まらず、エラーで返ることを確かめる回帰テスト(Issue #176)
#
# ContentTools.dllのImport()とImportFbx()をctypesで直接呼ぶ。GUIもGPUもFBX SDKも不要。
#   python ContentTools/Tests/missing_source_import_test.py                     # x64/DebugEditor/ContentTools.dll
#   python ContentTools/Tests/missing_source_import_test.py --config ReleaseEditor
#   python ContentTools/Tests/missing_source_import_test.py --dll <ContentTools.dllのパス>
# すべて合格すれば終了コード0、不合格があれば1。
#
# assertが有効なのはDebugのDLLだけなので、既定ではDebugEditorを使う(Releaseでは返り値しか確かめられない)。
# assertに当たるとプロセスごと止まるので、ケースごとに子プロセスで動かす。
# 子プロセスはassertのダイアログを標準エラー出力に流し、abortで終わるようにする(ダイアログは出ない)。
# 親はその終了コードと出力で「止まったこと」を検出する。止まったままのときはタイムアウトで不合格にする。
#
# 確かめること:
#   1. ないファイルを1つ渡すと import_error が file_not_found になる
#   2. 2つ目のソースがないときも file_not_found になる
#   3. ある画像を渡すと成功する(このテストの構造体の定義が正しいことの確認)
#   4. ないFBXを渡しても assert で止まらず、データを返さない
#   5. 壊れたFBX(中身がFBXでないファイル)を渡しても assert や落下をせず、データを返さない(Issue #189)
import argparse
import ctypes
import os
import struct
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

CHILD_TIMEOUT_SECONDS = 60
IMPORT_ERROR_SUCCEEDED = 0
IMPORT_ERROR_FILE_NOT_FOUND = 9  # import_error::file_not_found(TextureImportError.FileNotFound)
TEXTURE_2D = 1
IMAGE_WIDTH = 2
IMAGE_HEIGHT = 2


class TextureImportSettings(ctypes.Structure):
	_fields_ = [
		("sources", ctypes.c_char_p),
		("source_count", ctypes.c_uint32),
		("dimension", ctypes.c_uint32),
		("mip_levels", ctypes.c_uint32),
		("alpha_threshold", ctypes.c_float),
		("prefer_bc7", ctypes.c_uint32),
		("output_format", ctypes.c_uint32),
		("compress", ctypes.c_uint32),
		("cubemap_size", ctypes.c_uint32),
		("mirror_cubemap", ctypes.c_uint32),
		("prefilter_cubemap", ctypes.c_uint32),
	]


class TextureInfo(ctypes.Structure):
	_fields_ = [
		("width", ctypes.c_uint32),
		("height", ctypes.c_uint32),
		("array_size", ctypes.c_uint32),
		("mip_levels", ctypes.c_uint32),
		("format", ctypes.c_uint32),
		("import_error", ctypes.c_uint32),
		("flags", ctypes.c_uint32),
	]


class TextureData(ctypes.Structure):
	_fields_ = [
		("subresource_data", ctypes.c_void_p),
		("subresource_size", ctypes.c_uint32),
		("icon", ctypes.c_void_p),
		("icon_size", ctypes.c_uint32),
		("info", TextureInfo),
		("import_settings", TextureImportSettings),
	]


class GeometryImportSettings(ctypes.Structure):
	_fields_ = [
		("smoothing_angle", ctypes.c_float),
		("calculate_normals", ctypes.c_uint8),
		("calculate_tangents", ctypes.c_uint8),
		("reverse_handedness", ctypes.c_uint8),
		("import_embedded_textures", ctypes.c_uint8),
		("import_animations", ctypes.c_uint8),
		("coalesce_meshes", ctypes.c_uint8),
	]


class SceneData(ctypes.Structure):
	_fields_ = [
		("buffer", ctypes.c_void_p),
		("buffer_size", ctypes.c_uint32),
		("settings", GeometryImportSettings),
	]


def write_bmp(path):
	"""IMAGE_WIDTH x IMAGE_HEIGHT の24ビットBMPを書く(WICで読める最小の画像)"""
	row = b"\x40\x80\xc0" * IMAGE_WIDTH
	row += b"\0" * (-len(row) % 4)
	pixels = row * IMAGE_HEIGHT
	headers_size = 14 + 40
	file_header = struct.pack("<2sIHHI", b"BM", headers_size + len(pixels), 0, 0, headers_size)
	info_header = struct.pack("<IiiHHIIiiII", 40, IMAGE_WIDTH, IMAGE_HEIGHT, 1, 24, 0, len(pixels), 2835, 2835, 0, 0)
	with open(path, "wb") as f:
		f.write(file_header + info_header + pixels)


def load_tools(dll_path):
	tools = ctypes.CDLL(dll_path)
	# Debugビルドのassertはモーダルのダイアログを出して止まるので、標準エラー出力に流してabortさせる。
	kernel32 = ctypes.WinDLL("kernel32")
	kernel32.GetModuleHandleW.restype = ctypes.c_void_p
	debug_crt = bool(kernel32.GetModuleHandleW("ucrtbased.dll"))
	if debug_crt:
		ucrt = ctypes.CDLL("ucrtbased.dll")
		ucrt._set_error_mode(1)  # _OUT_TO_STDERR
		ucrt._set_abort_behavior(0, 3)  # _WRITE_ABORT_MSG | _CALL_REPORTFAULT
	print(f"debug_crt={int(debug_crt)}")
	return tools


def import_texture(tools, sources):
	data = TextureData()
	data.import_settings.sources = ";".join(sources).encode("mbcs")
	data.import_settings.source_count = len(sources)
	data.import_settings.dimension = TEXTURE_2D
	data.import_settings.mip_levels = 1
	data.import_settings.alpha_threshold = 0.5
	data.import_settings.compress = 0
	# WICの読み込みにはCOMの初期化が要る(エディタのWPFのスレッドは初期化済み)
	ctypes.windll.ole32.CoInitializeEx(None, 2)
	tools.Import(ctypes.byref(data))
	return data


def expect(condition, message):
	if not condition:
		print(f"NG: {message}")
		return False
	return True


def case_missing_single(tools, work_dir):
	missing = os.path.join(work_dir, "missing.png")
	error = import_texture(tools, [missing]).info.import_error
	return expect(error == IMPORT_ERROR_FILE_NOT_FOUND, f"import_error={error}(期待値 {IMPORT_ERROR_FILE_NOT_FOUND})")


def case_missing_second(tools, work_dir):
	existing = os.path.join(work_dir, "existing.bmp")
	missing = os.path.join(work_dir, "missing.png")
	error = import_texture(tools, [existing, missing]).info.import_error
	return expect(error == IMPORT_ERROR_FILE_NOT_FOUND, f"import_error={error}(期待値 {IMPORT_ERROR_FILE_NOT_FOUND})")


def case_existing(tools, work_dir):
	data = import_texture(tools, [os.path.join(work_dir, "existing.bmp")])
	info = data.info
	return (expect(info.import_error == IMPORT_ERROR_SUCCEEDED, f"import_error={info.import_error}(期待値 成功)") and
		expect((info.width, info.height) == (IMAGE_WIDTH, IMAGE_HEIGHT), f"大きさ {info.width}x{info.height}") and
		expect(bool(data.subresource_data) and data.subresource_size > 0, "サブリソースのデータがない"))


def case_missing_fbx(tools, work_dir):
	data = SceneData()
	data.settings = GeometryImportSettings(178.0, 0, 1, 0, 1, 1, 0)  # GeometryImportSettings(C#)の既定値
	missing = os.path.join(work_dir, "missing.fbx")
	tools.ImportFbx(missing.encode("mbcs"), ctypes.byref(data), None)
	return expect(not data.buffer and data.buffer_size == 0, f"データを返した: buffer_size={data.buffer_size}")


def case_broken_fbx(tools, work_dir):
	data = SceneData()
	data.settings = GeometryImportSettings(178.0, 0, 1, 0, 1, 1, 0)
	broken = os.path.join(work_dir, "broken.fbx")
	tools.ImportFbx(broken.encode("mbcs"), ctypes.byref(data), None)
	return expect(not data.buffer and data.buffer_size == 0, f"データを返した: buffer_size={data.buffer_size}")


CASES = {
	"ないファイルを1つ渡す": case_missing_single,
	"2つ目のソースがない": case_missing_second,
	"ある画像を渡す(対照)": case_existing,
	"ないFBXを渡す": case_missing_fbx,
	"壊れたFBXを渡す": case_broken_fbx,
}


def run_child(case_name, dll_path, work_dir):
	# 親から渡されたケースを1つ動かす。assertに当たると、ここには戻らずabortで終わる。
	sys.stdout.reconfigure(encoding="utf-8")
	tools = load_tools(dll_path)
	passed = CASES[case_name](tools, work_dir)
	sys.stdout.flush()
	return 0 if passed else 1


def run_case(case_name, dll_path, work_dir):
	"""(合格か, 理由, debug_crt)"""
	try:
		result = subprocess.run(
			[sys.executable, os.path.abspath(__file__), "--child", case_name, "--dll", dll_path, "--work-dir", work_dir],
			capture_output=True, timeout=CHILD_TIMEOUT_SECONDS,
			env={**os.environ, "PYTHONUTF8": "1"})
	except subprocess.TimeoutExpired:
		return False, f"{CHILD_TIMEOUT_SECONDS}秒で終わらない(assertのダイアログで止まっている可能性がある)", False

	stdout = result.stdout.decode("utf-8", errors="replace")
	stderr = result.stderr.decode("utf-8", errors="replace")
	debug_crt = "debug_crt=1" in stdout
	if "Assertion failed" in stderr:
		return False, f"assert で止まった(終了コード {result.returncode}):\n{stderr.strip()}", debug_crt
	if result.returncode != 0:
		return False, f"終了コード {result.returncode}:\n{stdout.strip()}\n{stderr.strip()}", debug_crt
	return True, "", debug_crt


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--config", default="DebugEditor")
	parser.add_argument("--dll")
	parser.add_argument("--child", choices=list(CASES), help=argparse.SUPPRESS)
	parser.add_argument("--work-dir", help=argparse.SUPPRESS)
	args = parser.parse_args()

	dll_path = os.path.abspath(args.dll or os.path.join(REPO_ROOT, "x64", args.config, "ContentTools.dll"))
	if args.child:
		return run_child(args.child, dll_path, args.work_dir)

	print(f"ContentTools.dll: {dll_path}")
	if not os.path.exists(dll_path):
		print("FAILED: ContentTools.dll がない")
		return 1

	passed = True
	assert_enabled = False
	with tempfile.TemporaryDirectory() as work_dir:
		write_bmp(os.path.join(work_dir, "existing.bmp"))
		with open(os.path.join(work_dir, "broken.fbx"), "wb") as f:
			f.write(b"this is not an fbx file\n" * 8)
		for case_name in CASES:
			case_passed, reason, debug_crt = run_case(case_name, dll_path, work_dir)
			assert_enabled = assert_enabled or debug_crt
			print(f"{'PASS' if case_passed else 'FAIL'} {case_name}")
			if not case_passed:
				passed = False
				print(f"  {reason}")

	if not assert_enabled:
		print("NOTE: このDLLはassertが無効なので、返り値だけを確かめた(assertを確かめるには DebugEditor を使う)")
	print("ALL PASSED" if passed else "FAILED")
	return 0 if passed else 1


if __name__ == "__main__":
	sys.exit(main())
