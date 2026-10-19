# 正距円筒 -> キューブマップ変換のCPU版とGPU版が一致することを確かめる回帰テスト(Issue #64)
#
# ContentTools.dllのImport()をctypesで直接呼ぶ。GUIもFBX SDKも不要(D3D11を使えるGPUは必要)。
#   python ContentTools/Tests/envmap_cpu_gpu_test.py                       # x64/ReleaseEditor/ContentTools.dll
#   python ContentTools/Tests/envmap_cpu_gpu_test.py --config DebugEditor  # x64/DebugEditor/ContentTools.dll
#   python ContentTools/Tests/envmap_cpu_gpu_test.py --dll <ContentTools.dllのパス>
# 一致すれば終了コード0、一致しなければ(またはGPU版で変換されたことを確かめられなければ)1、
# ハードウェアのD3D11デバイスを作れずGPU版を試せない環境では"SKIPPED"と表示して2を返す。
#
# 同じ格子模様のHDR画像を、GPU版とCPU版のそれぞれで変換し、画素ごとに比較する。
# CPU版はD3D11を使えない環境でのフォールバックなので、CPU版の子プロセスでは
# D3D11CreateDeviceをエクスポートしないDLLを"d3d11.dll"という名前で先に読み込んでおき、
# ContentTools.dllのLoadLibrary(L"d3d11.dll")がそれを返すようにしてGPU版を使えなくする。
#
# 判定: CPU版は最近傍、GPU版はバイリニアでサンプリングするので完全には一致しない。
# 両方が同じ位置(画素の中心)をサンプリングしていれば、最近傍のテクセルの色はバイリニアの
# 重みの半分以上を占めるため、差は格子の2色の差の半分以下に収まる。
# 半画素ずれていると、格子の境界でもう一方の色を拾い、差が2色の差のほぼ全体になる。
#
# GPU版の子プロセスでGPUが使えないと、ContentTools.dllは黙ってCPU版にフォールバックし、
# CPU版同士の比較で合格してしまう。そこで、GPU版の出力に2色の中間の値(バイリニアで補間された画素)が
# あることを確かめる。最近傍のCPU版は格子の2色以外の値を出さないので、これでGPUで変換されたと分かる。
import argparse
import ctypes
import ctypes.wintypes as wt
import os
import shutil
import struct
import subprocess
import sys
import tempfile

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

ENV_WIDTH = 64
ENV_HEIGHT = 32
CELL = 4
CUBEMAP_SIZE = 16
# RGBEで誤差なく表せる値(2のべき乗)にしておく。読み込むと仮数に0.5を足した値になる(from_rgbe)。
COLOR_A = (4.0, 0.25, 1.0)
COLOR_B = (0.25, 1.0, 4.0)
# GPUのバイリニアの重みの量子化などを見込んだ許容値(2色の差に対する割合)
TOLERANCE = 0.55

DXGI_FORMAT_R32G32B32A32_FLOAT = 2
TEXTURE_CUBE = 3


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
	_fields_ = [(name, ctypes.c_uint32) for name in
				("width", "height", "array_size", "mip_levels", "format", "import_error", "flags")]


class TextureData(ctypes.Structure):
	_fields_ = [
		("subresource_data", ctypes.POINTER(ctypes.c_uint8)),
		("subresource_size", ctypes.c_uint32),
		("icon", ctypes.POINTER(ctypes.c_uint8)),
		("icon_size", ctypes.c_uint32),
		("info", TextureInfo),
		("import_settings", TextureImportSettings),
	]


def checker_color(x, y):
	return COLOR_A if ((x // CELL) + (y // CELL)) % 2 == 0 else COLOR_B


def to_rgbe(color):
	m = max(color)
	exponent = 0
	while m >= 1.0:
		m /= 2.0
		exponent += 1
	while m < 0.5:
		m *= 2.0
		exponent -= 1
	scale = 256.0 / (2.0 ** exponent)
	rgb = [int(c * scale) for c in color]
	assert all(abs(v / scale - c) < 1e-9 for v, c in zip(rgb, color)), "RGBEで表せない色"
	return bytes(rgb + [exponent + 128])


def from_rgbe(rgbe):
	# DirectXTexのLoadFromHDRFileと同じく、仮数に0.5を足して復号する。
	scale = (2.0 ** (rgbe[3] - 128)) / 256.0
	return tuple((m + 0.5) * scale for m in rgbe[:3])


def write_checker_hdr(path):
	with open(path, "wb") as f:
		f.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n")
		f.write(f"-Y {ENV_HEIGHT} +X {ENV_WIDTH}\n".encode())
		for y in range(ENV_HEIGHT):
			for x in range(ENV_WIDTH):
				f.write(to_rgbe(checker_color(x, y)))


def suppress_debug_crt_dialogs():
	# Debugビルドのassertはモーダルのダイアログを出して止まるので、標準エラー出力に流す。
	try:
		ucrt = ctypes.CDLL("ucrtbased.dll")
	except OSError:
		return
	ucrt._set_error_mode(1)  # _OUT_TO_STDERR
	ucrt._set_abort_behavior(0, 3)  # _WRITE_ABORT_MSG | _CALL_REPORTFAULT


def module_path(name):
	kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
	kernel32.GetModuleHandleW.restype = wt.HMODULE
	handle = kernel32.GetModuleHandleW(name)
	if not handle:
		return None
	buffer = ctypes.create_unicode_buffer(1024)
	kernel32.GetModuleFileNameW(wt.HMODULE(handle), buffer, len(buffer))
	return buffer.value


def run_import(dll_path, hdr_path, mode, mirror, out_path):
	"""子プロセスで実行する。変換結果のfloat配列(6面 x size x size x RGBA)をout_pathに書く。"""
	if mode == "cpu":
		# D3D11CreateDeviceを持たないDLLを"d3d11.dll"として読み込んでおく。
		fake_dir = tempfile.mkdtemp(prefix="envmap_fake_d3d11_")
		fake = os.path.join(fake_dir, "d3d11.dll")
		shutil.copyfile(os.path.join(os.environ["SystemRoot"], "System32", "version.dll"), fake)
		ctypes.WinDLL(fake)

	ole32 = ctypes.WinDLL("ole32")
	ole32.CoInitializeEx(None, 2)  # COINIT_APARTMENTTHREADED (WICのため)

	tools = ctypes.CDLL(dll_path)
	suppress_debug_crt_dialogs()

	data = TextureData()
	settings = data.import_settings
	settings.sources = hdr_path.encode("mbcs")
	settings.source_count = 1
	settings.dimension = TEXTURE_CUBE
	settings.mip_levels = 1
	settings.alpha_threshold = 0.5
	settings.prefer_bc7 = 0
	settings.output_format = 0
	settings.compress = 0
	settings.cubemap_size = CUBEMAP_SIZE
	settings.mirror_cubemap = mirror
	settings.prefilter_cubemap = 0

	tools.Import(ctypes.byref(data))

	d3d11 = module_path("d3d11.dll")
	if mode == "cpu":
		assert d3d11 and d3d11.lower() == fake.lower(), f"CPU版の確認用のd3d11.dllが使われていない: {d3d11}"
	else:
		assert d3d11 and "system32" in d3d11.lower(), f"GPU版でD3D11が読み込まれていない: {d3d11}"

	info = data.info
	assert info.import_error == 0, f"import_error={info.import_error}"
	assert info.format == DXGI_FORMAT_R32G32B32A32_FLOAT, f"format={info.format}"
	assert (info.width, info.height, info.array_size, info.mip_levels) == (CUBEMAP_SIZE, CUBEMAP_SIZE, 6, 1), \
		f"size={info.width}x{info.height} array={info.array_size} mips={info.mip_levels}"

	blob = ctypes.string_at(data.subresource_data, data.subresource_size)
	pixels = []
	offset = 0
	for _ in range(6):
		width, height, row_pitch, slice_pitch = struct.unpack_from("<4I", blob, offset)
		offset += 16
		for y in range(height):
			row = struct.unpack_from(f"<{width * 4}f", blob, offset + y * row_pitch)
			pixels.extend(row)
		offset += slice_pitch

	ctypes.windll.ole32.CoTaskMemFree(data.subresource_data)
	if data.icon:
		ctypes.windll.ole32.CoTaskMemFree(data.icon)

	with open(out_path, "wb") as f:
		f.write(struct.pack(f"<{len(pixels)}f", *pixels))

	if mode == "cpu":
		shutil.rmtree(fake_dir, ignore_errors=True)


def convert(dll_path, hdr_path, mode, mirror, work_dir):
	out_path = os.path.join(work_dir, f"{mode}_{mirror}.bin")
	result = subprocess.run([sys.executable, os.path.abspath(__file__), "--child", mode, "--mirror", str(mirror),
							 "--dll", dll_path, "--hdr", hdr_path, "--out", out_path],
							capture_output=True, text=True, timeout=120)
	if result.returncode != 0:
		raise RuntimeError(f"{mode}版の変換に失敗しました (exit {result.returncode})\n{result.stdout}{result.stderr}")
	with open(out_path, "rb") as f:
		raw = f.read()
	return struct.unpack(f"<{len(raw) // 4}f", raw)


def has_hardware_d3d11():
	"""ハードウェア(WARPではない)のD3D11デバイスを、ContentTools.dllと同じ機能レベル(11_1, 11_0)で作れるか"""
	try:
		d3d11 = ctypes.WinDLL("d3d11")
	except OSError:
		return False
	feature_levels = (ctypes.c_uint32 * 2)(0xb100, 0xb000)  # D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0
	device = ctypes.c_void_p()
	hr = d3d11.D3D11CreateDevice(None, 1, None, 0, feature_levels, 2, 7,  # D3D_DRIVER_TYPE_HARDWARE, D3D11_SDK_VERSION
								 ctypes.byref(device), None, None)
	if hr < 0 or not device:
		return False
	# IUnknown::Release (vtableの3番目)
	release = ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.c_void_p)(ctypes.cast(device, ctypes.POINTER(ctypes.POINTER(ctypes.c_void_p)))[0][2])
	release(device)
	return True


def compare(cpu, gpu, mirror):
	color_a = from_rgbe(to_rgbe(COLOR_A))
	color_b = from_rgbe(to_rgbe(COLOR_B))
	contrast = [abs(a - b) for a, b in zip(color_a, color_b)]

	def is_checker_color(p):
		return all(abs(v - a) < 1e-6 for v, a in zip(p, color_a)) or all(abs(v - b) < 1e-6 for v, b in zip(p, color_b))

	pixel_count = len(cpu) // 4
	max_ratio = 0.0
	sum_ratio = 0.0
	bad = 0
	cpu_interpolated = 0
	gpu_interpolated = 0
	for i in range(pixel_count):
		c = cpu[i * 4:i * 4 + 3]
		g = gpu[i * 4:i * 4 + 3]
		if not is_checker_color(c):
			cpu_interpolated += 1
		if not is_checker_color(g):
			gpu_interpolated += 1
		ratio = max(abs(cv - gv) / k for cv, gv, k in zip(c, g, contrast))
		max_ratio = max(max_ratio, ratio)
		sum_ratio += ratio
		if ratio > TOLERANCE:
			bad += 1
	print(f"mirror={mirror}: 2色の差に対する最大誤差 {max_ratio:.3f}, 平均誤差 {sum_ratio / pixel_count:.4f}, "
		  f"許容値({TOLERANCE})を超えた画素 {bad}/{pixel_count}, 補間された画素 GPU版 {gpu_interpolated} / CPU版 {cpu_interpolated}")
	passed = bad == 0
	# CPU版は最近傍なので、出力は格子の2色のどちらかになるはず。
	if cpu_interpolated:
		print(f"  CPU版の出力に格子の色ではない画素が {cpu_interpolated} 個ある(CPU版で変換されていない疑い)")
		passed = False
	# GPU版はバイリニアなので、格子の境界では2色の中間の値になるはず。
	if not gpu_interpolated:
		print("  GPU版の出力に補間された画素がない(GPUが使えずCPU版にフォールバックした疑い)")
		passed = False
	return passed


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--config", default="ReleaseEditor")
	parser.add_argument("--dll")
	parser.add_argument("--child", choices=("cpu", "gpu"))
	parser.add_argument("--mirror", type=int, default=0)
	parser.add_argument("--hdr")
	parser.add_argument("--out")
	args = parser.parse_args()

	dll_path = os.path.abspath(args.dll or os.path.join(REPO_ROOT, "x64", args.config, "ContentTools.dll"))

	if args.child:
		run_import(dll_path, args.hdr, args.child, args.mirror, args.out)
		return 0

	print(f"ContentTools.dll: {dll_path}")
	if not has_hardware_d3d11():
		print("SKIPPED: ハードウェアのD3D11デバイスを作れないため、GPU版を試せません")
		return 2

	work_dir = tempfile.mkdtemp(prefix="envmap_cpu_gpu_test_")
	try:
		hdr_path = os.path.join(work_dir, "checker.hdr")
		write_checker_hdr(hdr_path)
		passed = True
		for mirror in (0, 1):
			gpu = convert(dll_path, hdr_path, "gpu", mirror, work_dir)
			cpu = convert(dll_path, hdr_path, "cpu", mirror, work_dir)
			passed = compare(cpu, gpu, mirror) and passed
	finally:
		shutil.rmtree(work_dir, ignore_errors=True)

	print("PASSED" if passed else "FAILED")
	return 0 if passed else 1


if __name__ == "__main__":
	sys.exit(main())
