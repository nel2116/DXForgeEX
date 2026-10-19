# 拡散光のIBL用の事前フィルタ(irradiance map, Issue #35)のCPU版とGPU版を確かめる回帰テスト
#
# ContentTools.dllのPrefilterDiffuseIBL()などをctypesで直接呼ぶ。GUIもFBX SDKも不要(D3D11を使えるGPUは必要)。
#   python ContentTools/Tests/irradiance_cpu_gpu_test.py                       # x64/ReleaseEditor/ContentTools.dll
#   python ContentTools/Tests/irradiance_cpu_gpu_test.py --config DebugEditor  # x64/DebugEditor/ContentTools.dll
#   python ContentTools/Tests/irradiance_cpu_gpu_test.py --dll <ContentTools.dllのパス>
# すべて合格すれば終了コード0、不合格があれば1、
# ハードウェアのD3D11デバイスを作れずGPU版を試せない環境では"SKIPPED"と表示して2を返す。
#
# CPU版を使わせる方法(D3D11CreateDeviceを持たない"d3d11.dll"を先に読み込む)は envmap_cpu_gpu_test.py と同じ。
#
# 確かめること:
#   1. 答えがわかる入力での値(CPU版とGPU版のそれぞれ)
#      - 一様な色cのキューブマップ -> 出力もすべてc
#      - 放射輝度が方向の1次関数 L(w) = a + b・w のキューブマップ -> 出力は a + (2/3) b・n
#        (cosθで重み付けた半球の平均。∫(b・w)cosθ dw = (2π/3) b・n、∫cosθ dw = π)
#        方向はD3Dのキューブマップの面の配置から求めるので、GPU版がシェーダのTextureCubeを
#        正しい向きでサンプリングしていることも確かめられる。
#      - キューブマップ2つ(配列のサイズ12)を1回で処理し、2つ目の結果も正しいこと
#   2. CPU版とGPU版が誤差の範囲で一致すること(乱数の模様の入力で画素ごとに比較)
#      CPU版は倍精度、GPU版は単精度で和を求めるので、完全には一致しない。完全に一致した場合は、
#      GPU版の子プロセスでGPUが使えずCPU版にフォールバックした疑いがあるので不合格にする。
#   3. エディタと同じ流れ(6枚のHDR画像をImport() -> 結果のコピーにPrefilterDiffuseIBL()、
#      元にPrefilterSpecularIBL())で、圧縮(BC6H)まで行われ、値が正しいこと
#   4. 大きな入力(1024の6面)でも、入力を縮小してから総当たりするので処理時間が抑えられること。
#      一様な色 -> 同じ色、面ごとに色が違う入力でCPU版とGPU版が一致すること(どちらも同じ縮小を通る)
import argparse
import ctypes
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import time

import envmap_cpu_gpu_test as envmap
from envmap_cpu_gpu_test import TextureData

REPO_ROOT = envmap.REPO_ROOT

IN_SIZE = 32
OUT_SIZE = 64
UNIFORM_COLOR = (1.5, 0.25, 3.0)
LINEAR_A = 0.5
LINEAR_B = 0.4
# 1. 一様な色: 重みの和で割るので、丸め誤差だけ(相対誤差, 実測 GPU版5.5e-6 / CPU版0)
UNIFORM_TOLERANCE = 5e-5
# 1. 1次関数: 丸め誤差と、入力の画素の中心だけで積分する離散化の誤差(絶対誤差, 実測 GPU版1.4e-5 / CPU版1.2e-5)
LINEAR_TOLERANCE = 1e-4
# 2. CPU版とGPU版の差(相対誤差)。単精度で6 * 32 * 32個の和を求める丸め誤差を見込む(実測5.5e-6)。
CPU_GPU_TOLERANCE = 1e-4
# 3. BC6Hに圧縮してから解凍した値の誤差(相対誤差)
BC6H_TOLERANCE = 0.02
# 3. RGBEで誤差なく表せる値(2のべき乗)
IMPORT_COLOR = (4.0, 0.25, 1.0)
# 4. 大きな入力。縮小しないと出力64x64x6画素 x 入力1024x1024x6画素 = 約1.5e11回のループになる。
LARGE_SIZE = 1024
LARGE_FACE_COLORS = ((4.0, 0.5, 0.5), (0.5, 4.0, 0.5), (0.5, 0.5, 4.0), (2.0, 2.0, 0.25), (0.25, 2.0, 2.0), (2.0, 0.25, 2.0))
# 4. 2つのキューブマップ(一様な色と面ごとの色)を1回で処理する時間の上限(秒)
LARGE_TIME_LIMIT = 10.0

DXGI_FORMAT_R32G32B32A32_FLOAT = 2
DXGI_FORMAT_BC6H_UF16 = 95
TEXTURE_CUBE = 3
FLAG_IS_HDR = 0x01
FLAG_IS_CUBE_MAP = 0x10


def cube_direction(face, x, y, size):
	"""D3Dのキューブマップの面faceの画素(x, y)の中心の方向(正規化済み)。
	D3Dの仕様では、主軸が+Xの方向(x, y, z)は面上の座標 s = (-z/|x| + 1) / 2, t = (-y/|x| + 1) / 2 に写る。
	その逆写像で、u = 2s - 1, v = 2t - 1 から方向を求める。"""
	u = 2.0 * (x + 0.5) / size - 1.0
	v = 2.0 * (y + 0.5) / size - 1.0
	d = [
		(1.0, -v, -u),   # +X: s = -z/x,  t = -y/x
		(-1.0, -v, u),   # -X: s =  z/|x|, t = -y/|x|
		(u, 1.0, v),     # +Y: s =  x/y,  t =  z/y
		(u, -1.0, -v),   # -Y: s =  x/|y|, t = -z/|y|
		(u, -v, 1.0),    # +Z: s =  x/z,  t = -y/z
		(-u, -v, -1.0),  # -Z: s = -x/|z|, t = -y/|z|
	][face]
	length = sum(c * c for c in d) ** 0.5
	return tuple(c / length for c in d)


def linear_radiance(direction):
	return tuple(LINEAR_A + LINEAR_B * c for c in direction)


def linear_expected(normal):
	return tuple(LINEAR_A + LINEAR_B * (2.0 / 3.0) * c for c in normal)


def make_cubemap(kind, seed=0):
	"""1つのキューブマップのミップ0の6面(各面はIN_SIZE x IN_SIZEのRGBAのリスト)"""
	rng = random.Random(seed)
	faces = []
	for face in range(6):
		pixels = []
		for y in range(IN_SIZE):
			for x in range(IN_SIZE):
				if kind == "uniform":
					rgb = UNIFORM_COLOR
				elif kind == "linear":
					rgb = linear_radiance(cube_direction(face, x, y, IN_SIZE))
				else:
					rgb = tuple(rng.uniform(0.0, 4.0) for _ in range(3))
				pixels.append(rgb + (1.0,))
		faces.append(pixels)
	return faces


def build_subresources(cubemaps):
	"""Import()の出力と同じ並び([配列][ミップ]、ミップはすべて)のサブリソースのバイト列。
	サンプリングするのはミップ0だけなので、ミップ1以降は0で埋める(使われていれば結果がずれる)。"""
	mip_levels = IN_SIZE.bit_length()
	blob = bytearray()
	for faces in cubemaps:
		for pixels in faces:
			for mip in range(mip_levels):
				size = max(IN_SIZE >> mip, 1)
				row_pitch = size * 16
				blob += struct.pack("<4I", size, size, row_pitch, row_pitch * size)
				if mip == 0:
					blob += struct.pack(f"<{len(pixels) * 4}f", *[c for p in pixels for c in p])
				else:
					blob += bytes(row_pitch * size)
	return bytes(blob), mip_levels


def build_constant_subresources(cubemaps, size):
	"""build_subresources()と同じ並びで、各面が1色(cubemaps = [キューブマップ][面] = rgb)のバイト列"""
	mip_levels = size.bit_length()
	blob = bytearray()
	for face_colors in cubemaps:
		for rgb in face_colors:
			for mip in range(mip_levels):
				mip_size = max(size >> mip, 1)
				row_pitch = mip_size * 16
				blob += struct.pack("<4I", mip_size, mip_size, row_pitch, row_pitch * mip_size)
				if mip == 0:
					blob += struct.pack("<4f", *rgb, 1.0) * (mip_size * mip_size)
				else:
					blob += bytes(row_pitch * mip_size)
	return bytes(blob), mip_levels


def read_subresources(data):
	"""サブリソースを[画像][画素] = (r, g, b, a)のリストにする(R32G32B32A32_FLOATのみ)"""
	blob = ctypes.string_at(data.subresource_data, data.subresource_size)
	images = []
	offset = 0
	while offset < len(blob):
		width, height, row_pitch, slice_pitch = struct.unpack_from("<4I", blob, offset)
		offset += 16
		pixels = []
		for y in range(height):
			row = struct.unpack_from(f"<{width * 4}f", blob, offset + y * row_pitch)
			pixels.extend(tuple(row[i:i + 4]) for i in range(0, len(row), 4))
		images.append(pixels)
		offset += slice_pitch
	return images


def co_task_mem_copy(raw):
	ole32 = ctypes.windll.ole32
	ole32.CoTaskMemAlloc.restype = ctypes.c_void_p
	ptr = ole32.CoTaskMemAlloc(len(raw))
	assert ptr
	ctypes.memmove(ptr, raw, len(raw))
	return ctypes.cast(ptr, ctypes.POINTER(ctypes.c_uint8))


def free_texture_data(data):
	if data.subresource_data:
		ctypes.windll.ole32.CoTaskMemFree(data.subresource_data)
	if data.icon:
		ctypes.windll.ole32.CoTaskMemFree(data.icon)


def set_settings(settings, compress):
	settings.dimension = TEXTURE_CUBE
	settings.mip_levels = 0
	settings.alpha_threshold = 0.5
	settings.prefer_bc7 = 0
	settings.output_format = 0
	settings.compress = compress
	settings.cubemap_size = IN_SIZE
	settings.mirror_cubemap = 0
	settings.prefilter_cubemap = 1


def prefilter_direct(tools, cubemaps):
	"""サブリソースを直接組み立ててPrefilterDiffuseIBL()を呼び、出力の画像を返す"""
	raw, mip_levels = build_subresources(cubemaps)
	return prefilter_subresources(tools, raw, mip_levels, IN_SIZE, len(cubemaps))


def prefilter_subresources(tools, raw, mip_levels, size, cubemap_count):
	data = TextureData()
	data.subresource_data = co_task_mem_copy(raw)
	data.subresource_size = len(raw)
	info = data.info
	info.width = info.height = size
	info.array_size = 6 * cubemap_count
	info.mip_levels = mip_levels
	info.format = DXGI_FORMAT_R32G32B32A32_FLOAT
	info.flags = FLAG_IS_HDR | FLAG_IS_CUBE_MAP
	set_settings(data.import_settings, 0)

	tools.PrefilterDiffuseIBL(ctypes.byref(data))

	try:
		assert info.import_error == 0, f"import_error={info.import_error}"
		assert info.format == DXGI_FORMAT_R32G32B32A32_FLOAT, f"format={info.format}"
		assert (info.width, info.height, info.array_size, info.mip_levels) == (OUT_SIZE, OUT_SIZE, 6 * cubemap_count, 1), \
			f"size={info.width}x{info.height} array={info.array_size} mips={info.mip_levels}"
		return read_subresources(data)
	finally:
		free_texture_data(data)


def write_uniform_hdr(path, size, color):
	with open(path, "wb") as f:
		f.write(b"#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n")
		f.write(f"-Y {size} +X {size}\n".encode())
		f.write(envmap.to_rgbe(color) * (size * size))


def clone_texture_data(src):
	dst = TextureData()
	ctypes.memmove(ctypes.byref(dst), ctypes.byref(src), ctypes.sizeof(TextureData))
	raw = ctypes.string_at(src.subresource_data, src.subresource_size)
	dst.subresource_data = co_task_mem_copy(raw)
	dst.icon = None
	dst.icon_size = 0
	return dst


def import_like_editor(tools, work_dir):
	"""エディタと同じ流れでインポートし、(拡散光のIBLを解凍した画像, ログ)を返す"""
	paths = []
	for face in range(6):
		path = os.path.join(work_dir, f"face{face}.hdr")
		write_uniform_hdr(path, IN_SIZE, IMPORT_COLOR)
		paths.append(path)

	data = TextureData()
	sources = ";".join(paths).encode("mbcs")
	data.import_settings.sources = sources
	data.import_settings.source_count = 6
	set_settings(data.import_settings, 1)
	tools.Import(ctypes.byref(data))
	info = data.info
	assert info.import_error == 0, f"Import: import_error={info.import_error}"
	# プリフィルタするキューブマップは、Import()では圧縮されず、すべてのミップレベルを持つ。
	assert info.format == DXGI_FORMAT_R32G32B32A32_FLOAT, f"Import: format={info.format}"
	assert (info.width, info.array_size, info.mip_levels) == (IN_SIZE, 6, IN_SIZE.bit_length()), \
		f"Import: size={info.width} array={info.array_size} mips={info.mip_levels}"

	diffuse = clone_texture_data(data)
	try:
		tools.PrefilterDiffuseIBL(ctypes.byref(diffuse))
		tools.PrefilterSpecularIBL(ctypes.byref(data))
		assert diffuse.info.import_error == 0, f"PrefilterDiffuseIBL: import_error={diffuse.info.import_error}"
		assert data.info.import_error == 0, f"PrefilterSpecularIBL: import_error={data.info.import_error}"
		assert diffuse.info.format == DXGI_FORMAT_BC6H_UF16, f"PrefilterDiffuseIBL: format={diffuse.info.format}"
		assert (diffuse.info.width, diffuse.info.array_size, diffuse.info.mip_levels) == (OUT_SIZE, 6, 1)
		assert diffuse.icon and diffuse.icon_size, "PrefilterDiffuseIBL: アイコンがない"
		# 鏡面反射のフィルタは未実装(#36)。後回しにした圧縮だけが行われる。
		assert data.info.format == DXGI_FORMAT_BC6H_UF16, f"PrefilterSpecularIBL: format={data.info.format}"
		assert (data.info.width, data.info.array_size, data.info.mip_levels) == (IN_SIZE, 6, IN_SIZE.bit_length())

		tools.Decompress(ctypes.byref(diffuse))
		assert diffuse.info.import_error == 0, f"Decompress: import_error={diffuse.info.import_error}"
		assert diffuse.info.format == DXGI_FORMAT_R32G32B32A32_FLOAT, f"Decompress: format={diffuse.info.format}"
		return read_subresources(diffuse)
	finally:
		free_texture_data(diffuse)
		free_texture_data(data)


def run_child(dll_path, mode, out_path):
	"""子プロセスで実行する。各ケースの出力をout_pathに書く。"""
	if mode == "cpu":
		fake_dir = tempfile.mkdtemp(prefix="irradiance_fake_d3d11_")
		fake = os.path.join(fake_dir, "d3d11.dll")
		shutil.copyfile(os.path.join(os.environ["SystemRoot"], "System32", "version.dll"), fake)
		ctypes.WinDLL(fake)

	ctypes.WinDLL("ole32").CoInitializeEx(None, 2)  # COINIT_APARTMENTTHREADED (WICのため)
	tools = ctypes.CDLL(dll_path)
	envmap.suppress_debug_crt_dialogs()

	results = {
		"mixed": prefilter_direct(tools, [make_cubemap("linear"), make_cubemap("uniform")]),
		"random": prefilter_direct(tools, [make_cubemap("random", seed=35)]),
	}
	raw, mip_levels = build_constant_subresources([[UNIFORM_COLOR] * 6, LARGE_FACE_COLORS], LARGE_SIZE)
	start = time.perf_counter()
	results["large"] = prefilter_subresources(tools, raw, mip_levels, LARGE_SIZE, 2)
	results["large_seconds"] = time.perf_counter() - start
	del raw
	work_dir = tempfile.mkdtemp(prefix="irradiance_import_")
	try:
		results["import"] = import_like_editor(tools, work_dir)
	finally:
		shutil.rmtree(work_dir, ignore_errors=True)

	d3d11 = envmap.module_path("d3d11.dll")
	if mode == "cpu":
		assert d3d11 and d3d11.lower() == fake.lower(), f"CPU版の確認用のd3d11.dllが使われていない: {d3d11}"
	else:
		assert d3d11 and "system32" in d3d11.lower(), f"GPU版でD3D11が読み込まれていない: {d3d11}"

	with open(out_path, "w", encoding="utf-8") as f:
		f.write(repr(results))

	if mode == "cpu":
		shutil.rmtree(fake_dir, ignore_errors=True)


def run(dll_path, mode, work_dir):
	out_path = os.path.join(work_dir, f"{mode}.txt")
	result = subprocess.run([sys.executable, os.path.abspath(__file__), "--child", mode, "--dll", dll_path, "--out", out_path],
							capture_output=True, text=True, timeout=600)
	if result.returncode != 0:
		raise RuntimeError(f"{mode}版の実行に失敗しました (exit {result.returncode})\n{result.stdout}{result.stderr}")
	with open(out_path, encoding="utf-8") as f:
		return eval(f.read())


def max_error(images, expected, relative):
	"""expected(face, x, y) -> rgb との最大誤差"""
	worst = 0.0
	for face, pixels in enumerate(images):
		for i, p in enumerate(pixels):
			e = expected(face, i % OUT_SIZE, i // OUT_SIZE)
			for v, ev in zip(p[:3], e):
				worst = max(worst, abs(v - ev) / (abs(ev) if relative else 1.0))
	return worst


def check(label, error, tolerance):
	passed = error <= tolerance
	print(f"  {label}: 最大誤差 {error:.3g} (許容値 {tolerance:g}) {'OK' if passed else 'NG'}")
	return passed


def check_known_answers(mode, results):
	print(f"{mode}版:")
	mixed = results["mixed"]
	passed = check("1次関数の入力 -> a + (2/3) b・n", max_error(mixed[:6], lambda f, x, y: linear_expected(cube_direction(f, x, y, OUT_SIZE)), False),
				   LINEAR_TOLERANCE)
	passed = check("一様な色の入力(2つ目のキューブマップ) -> 同じ色", max_error(mixed[6:], lambda f, x, y: UNIFORM_COLOR, True),
				   UNIFORM_TOLERANCE) and passed
	import_color = envmap.from_rgbe(envmap.to_rgbe(IMPORT_COLOR))
	passed = check("Import -> PrefilterDiffuseIBL -> BC6H -> Decompress(一様な色)", max_error(results["import"], lambda f, x, y: import_color, True),
				   BC6H_TOLERANCE) and passed
	passed = check(f"{LARGE_SIZE}の6面の一様な色の入力 -> 同じ色", max_error(results["large"][:6], lambda f, x, y: UNIFORM_COLOR, True),
				   UNIFORM_TOLERANCE) and passed
	seconds = results["large_seconds"]
	time_ok = seconds <= LARGE_TIME_LIMIT
	print(f"  {LARGE_SIZE}の6面のキューブマップ2つの処理時間: {seconds:.2f}秒 (上限 {LARGE_TIME_LIMIT:g}秒) {'OK' if time_ok else 'NG'}")
	passed = passed and time_ok
	alpha_ok = all(p[3] == 1.0 for images in (mixed, results["random"]) for pixels in images for p in pixels)
	print(f"  出力のアルファがすべて1: {'OK' if alpha_ok else 'NG'}")
	return passed and alpha_ok


def check_cpu_gpu(cpu, gpu):
	print("CPU版とGPU版の比較:")
	passed = True
	for case in ("mixed", "random", "large"):
		c_images, g_images = cpu[case], gpu[case]
		worst = 0.0
		identical = True
		for c_pixels, g_pixels in zip(c_images, g_images):
			for c, g in zip(c_pixels, g_pixels):
				identical = identical and c == g
				worst = max(worst, max(abs(cv - gv) / max(abs(cv), 1e-6) for cv, gv in zip(c[:3], g[:3])))
		passed = check(case, worst, CPU_GPU_TOLERANCE) and passed
		if case == "random" and identical:
			print("  GPU版とCPU版の出力が完全に一致した(GPUが使えずCPU版にフォールバックした疑い)")
			passed = False
	return passed


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--config", default="ReleaseEditor")
	parser.add_argument("--dll")
	parser.add_argument("--child", choices=("cpu", "gpu"))
	parser.add_argument("--out")
	args = parser.parse_args()

	dll_path = os.path.abspath(args.dll or os.path.join(REPO_ROOT, "x64", args.config, "ContentTools.dll"))

	if args.child:
		run_child(dll_path, args.child, args.out)
		return 0

	print(f"ContentTools.dll: {dll_path}")
	if not envmap.has_hardware_d3d11():
		print("SKIPPED: ハードウェアのD3D11デバイスを作れないため、GPU版を試せません")
		return 2

	work_dir = tempfile.mkdtemp(prefix="irradiance_cpu_gpu_test_")
	try:
		gpu = run(dll_path, "gpu", work_dir)
		cpu = run(dll_path, "cpu", work_dir)
	finally:
		shutil.rmtree(work_dir, ignore_errors=True)

	passed = check_known_answers("GPU", gpu)
	passed = check_known_answers("CPU", cpu) and passed
	passed = check_cpu_gpu(cpu, gpu) and passed

	print("PASSED" if passed else "FAILED")
	return 0 if passed else 1


if __name__ == "__main__":
	sys.exit(main())
