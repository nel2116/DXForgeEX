# 鏡面反射のIBL用の事前フィルタ(ラフネスごとのミップ, Issue #36)のCPU版とGPU版を確かめる回帰テスト
#
# ContentTools.dllのPrefilterSpecularIBL()などをctypesで直接呼ぶ。GUIもFBX SDKも不要(D3D11を使えるGPUは必要)。
#   python ContentTools/Tests/specular_cpu_gpu_test.py                       # x64/ReleaseEditor/ContentTools.dll
#   python ContentTools/Tests/specular_cpu_gpu_test.py --config DebugEditor  # x64/DebugEditor/ContentTools.dll
#   python ContentTools/Tests/specular_cpu_gpu_test.py --dll <ContentTools.dllのパス>
# すべて合格すれば終了コード0、不合格があれば1、
# ハードウェアのD3D11デバイスを作れずGPU版を試せない環境では"SKIPPED"と表示して2を返す。
#
# CPU版を使わせる方法(D3D11CreateDeviceを持たない"d3d11.dll"を先に読み込む)は envmap_cpu_gpu_test.py と同じ。
#
# 出力は1面が256x256でミップ6つのキューブマップ。ミップmはラフネスm / 5で、ミップ0は入力そのもの。
# 確かめること:
#   1. 答えがわかる入力での値(CPU版とGPU版のそれぞれ)
#      - 一様な色cのキューブマップ -> すべてのミップがc
#      - ミップ0(ラフネス0)は入力(1面256x256)と完全に一致
#      - 放射輝度が方向の1次関数 L(w) = a + b・w のキューブマップ -> ミップmの画素(方向n)は
#        a + b・(Σ NoL_i L_i / Σ NoL_i)。L_iはシェーダと同じGGXの重点サンプリングの方向(N = V = n)で、
#        ここでは倍精度で直接求める。シェーダとの違いは、テクスチャの離散化(バイリニアとミップ)の誤差だけ。
#        方向はD3Dのキューブマップの面の配置から求めるので、GPU版がTextureCubeを正しい向きでサンプリングしていること、
#        CPU版のキューブマップのサンプリング(面の選び方、面の境目)が正しいことも確かめられる。
#      - キューブマップ2つ(配列のサイズ12)を1回で処理し、2つ目の結果も正しいこと
#      - ラフネスが大きいミップほどぼける(乱数の模様の入力で、画素の値の分散が小さくなる)
#   2. CPU版とGPU版が誤差の範囲で一致すること(乱数の模様の入力で画素ごとに比較)。
#      GPU版はハードウェアのテクスチャフィルタ(重みの精度が低い)を使うので、完全には一致しない。
#      完全に一致した場合は、GPU版の子プロセスでGPUが使えずCPU版にフォールバックした疑いがあるので不合格にする。
#   3. エディタと同じ流れ(6枚のHDR画像をImport() -> PrefilterSpecularIBL())で、圧縮(BC6H)まで行われ、値が正しいこと
#   4. 大きな入力(1024の6面)でも、入力を256に縮小してから処理するので処理時間が抑えられること。
#      一様な色 -> 同じ色、面ごとに色が違う入力でCPU版とGPU版が一致すること(どちらも同じ縮小を通る)
import argparse
import array
import ctypes
import math
import os
import pickle
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import time

import envmap_cpu_gpu_test as envmap
import irradiance_cpu_gpu_test as irradiance
from envmap_cpu_gpu_test import TextureData

REPO_ROOT = envmap.REPO_ROOT

OUT_SIZE = 256
OUT_MIPS = 6
SAMPLE_COUNT = 1024
UNIFORM_COLOR = (1.5, 0.25, 3.0)
LINEAR_A = 0.5
LINEAR_B = 0.4
# 1. 一様な色: 重みの和で割るので、丸め誤差だけ(相対誤差, 実測 GPU版5.6e-7 / CPU版0)
UNIFORM_TOLERANCE = 1e-5
# 1. 1次関数: テクスチャの離散化(バイリニアとボックスフィルタのミップ)の誤差(絶対誤差)。
#    面の向きやサンプリング方向を間違えると、誤差はLINEAR_B(0.4)程度になる。(実測 GPU版 / CPU版ともに1.2e-3)
LINEAR_TOLERANCE = 0.005
# 1. 1次関数の確認に使う画素: 各ミップの各面で、縦横それぞれこの数の画素を等間隔に選ぶ
LINEAR_CHECK_GRID = 6
# 2. CPU版とGPU版の差(相対誤差)。GPUのバイリニアの重みやミップの補間の精度は低い(D3D11では8ビット程度)。
#    実測は最大2.9e-4(乱数の模様2.3e-4、面ごとの色2.9e-4)。
CPU_GPU_TOLERANCE = 2e-3
# 3. BC6Hに圧縮してから解凍した値の誤差(相対誤差)
BC6H_TOLERANCE = 0.02
IMPORT_SIZE = 64
IMPORT_COLOR = irradiance.IMPORT_COLOR
# 4. 大きな入力
LARGE_SIZE = 1024
LARGE_FACE_COLORS = irradiance.LARGE_FACE_COLORS
# 4. 2つのキューブマップ(一様な色と面ごとの色)を1回で処理する時間の上限(秒)。
#    処理量は入力のサイズによらない(1キューブマップあたり 出力の(128^2 + 64^2 + ... + 8^2) x 6画素 x 1024サンプル)。
#    実測はDebugEditorでGPU版1.0秒 / CPU版12秒(最適化なし)、ReleaseEditorでGPU版0.51秒 / CPU版2.8秒。
LARGE_TIME_LIMIT = 30.0

DXGI_FORMAT_R32G32B32A32_FLOAT = 2
DXGI_FORMAT_BC6H_UF16 = 95
FLAG_IS_HDR = 0x01
FLAG_IS_CUBE_MAP = 0x10

cube_direction = irradiance.cube_direction


def linear_radiance(direction):
	return tuple(LINEAR_A + LINEAR_B * c for c in direction)


def build_cubemap_blob(face_pixel, size):
	"""1つのキューブマップのミップ0の6面のバイト列(face_pixel(face, x, y) -> rgb)。ミップ0だけを持つ。"""
	blob = bytearray()
	row_pitch = size * 16
	for face in range(6):
		blob += struct.pack("<4I", size, size, row_pitch, row_pitch * size)
		values = array.array("f")
		for y in range(size):
			for x in range(size):
				values.extend(face_pixel(face, x, y))
				values.append(1.0)
		blob += values.tobytes()
	return bytes(blob)


def build_constant_cubemap_blob(face_colors, size):
	"""build_cubemap_blob()と同じ並びで、各面が1色のバイト列"""
	row_pitch = size * 16
	return b"".join(struct.pack("<4I", size, size, row_pitch, row_pitch * size) + struct.pack("<4f", *rgb, 1.0) * (size * size)
					for rgb in face_colors)


def make_cubemap(kind, size=OUT_SIZE, seed=0):
	rng = random.Random(seed)
	if kind == "uniform":
		return build_constant_cubemap_blob([UNIFORM_COLOR] * 6, size)
	if kind == "linear":
		return build_cubemap_blob(lambda f, x, y: linear_radiance(cube_direction(f, x, y, size)), size)
	if kind == "faces":
		return build_constant_cubemap_blob(LARGE_FACE_COLORS, size)
	assert kind == "random"
	return build_cubemap_blob(lambda f, x, y: (rng.uniform(0.0, 4.0), rng.uniform(0.0, 4.0), rng.uniform(0.0, 4.0)), size)


def prefilter_cubemaps(tools, blobs, size):
	"""ミップ0だけのキューブマップ(build_cubemap_blob())をPrefilterSpecularIBL()でフィルタし、出力のサブリソースのバイト列を返す"""
	raw = b"".join(blobs)
	data = TextureData()
	data.subresource_data = irradiance.co_task_mem_copy(raw)
	data.subresource_size = len(raw)
	info = data.info
	info.width = info.height = size
	info.array_size = 6 * len(blobs)
	info.mip_levels = 1
	info.format = DXGI_FORMAT_R32G32B32A32_FLOAT
	info.flags = FLAG_IS_HDR | FLAG_IS_CUBE_MAP
	irradiance.set_settings(data.import_settings, 0)

	tools.PrefilterSpecularIBL(ctypes.byref(data))

	try:
		assert info.import_error == 0, f"import_error={info.import_error}"
		assert info.format == DXGI_FORMAT_R32G32B32A32_FLOAT, f"format={info.format}"
		assert (info.width, info.height, info.array_size, info.mip_levels) == (OUT_SIZE, OUT_SIZE, 6 * len(blobs), OUT_MIPS), \
			f"size={info.width}x{info.height} array={info.array_size} mips={info.mip_levels}"
		return ctypes.string_at(data.subresource_data, data.subresource_size)
	finally:
		irradiance.free_texture_data(data)


def parse_images(blob):
	"""サブリソースのバイト列を[画像] = (幅, 高さ, 詰めたRGBAのfloatの配列)にする。並びは[配列][ミップ]。"""
	images = []
	offset = 0
	while offset < len(blob):
		width, height, row_pitch, slice_pitch = struct.unpack_from("<4I", blob, offset)
		offset += 16
		values = array.array("f")
		for y in range(height):
			values.frombytes(blob[offset + y * row_pitch:offset + y * row_pitch + width * 16])
		images.append((width, height, values))
		offset += slice_pitch
	return images


def image_at(images, cubemap, face, mip):
	return images[(cubemap * 6 + face) * OUT_MIPS + mip]


def import_like_editor(tools, work_dir):
	"""エディタと同じ流れでインポートし、鏡面反射のIBLを解凍したサブリソースのバイト列を返す"""
	paths = []
	for face in range(6):
		path = os.path.join(work_dir, f"face{face}.hdr")
		irradiance.write_uniform_hdr(path, IMPORT_SIZE, IMPORT_COLOR)
		paths.append(path)

	data = TextureData()
	sources = ";".join(paths).encode("mbcs")
	data.import_settings.sources = sources
	data.import_settings.source_count = 6
	irradiance.set_settings(data.import_settings, 1)
	tools.Import(ctypes.byref(data))
	info = data.info
	try:
		assert info.import_error == 0, f"Import: import_error={info.import_error}"
		# プリフィルタするキューブマップは、Import()では圧縮されず、すべてのミップレベルを持つ。
		assert info.format == DXGI_FORMAT_R32G32B32A32_FLOAT, f"Import: format={info.format}"
		assert (info.width, info.array_size, info.mip_levels) == (IMPORT_SIZE, 6, IMPORT_SIZE.bit_length()), \
			f"Import: size={info.width} array={info.array_size} mips={info.mip_levels}"

		tools.PrefilterSpecularIBL(ctypes.byref(data))
		assert info.import_error == 0, f"PrefilterSpecularIBL: import_error={info.import_error}"
		assert info.format == DXGI_FORMAT_BC6H_UF16, f"PrefilterSpecularIBL: format={info.format}"
		assert (info.width, info.array_size, info.mip_levels) == (OUT_SIZE, 6, OUT_MIPS), \
			f"PrefilterSpecularIBL: size={info.width} array={info.array_size} mips={info.mip_levels}"
		assert data.icon and data.icon_size, "PrefilterSpecularIBL: アイコンがない"

		tools.Decompress(ctypes.byref(data))
		assert info.import_error == 0, f"Decompress: import_error={info.import_error}"
		assert info.format == DXGI_FORMAT_R32G32B32A32_FLOAT, f"Decompress: format={info.format}"
		return ctypes.string_at(data.subresource_data, data.subresource_size)
	finally:
		irradiance.free_texture_data(data)


def run_child(dll_path, mode, out_path):
	"""子プロセスで実行する。各ケースの出力をout_pathに書く。"""
	if mode == "cpu":
		fake_dir = tempfile.mkdtemp(prefix="specular_fake_d3d11_")
		fake = os.path.join(fake_dir, "d3d11.dll")
		shutil.copyfile(os.path.join(os.environ["SystemRoot"], "System32", "version.dll"), fake)
		ctypes.WinDLL(fake)

	ctypes.WinDLL("ole32").CoInitializeEx(None, 2)  # COINIT_APARTMENTTHREADED (WICのため)
	tools = ctypes.CDLL(dll_path)
	envmap.suppress_debug_crt_dialogs()

	results = {}
	blobs = [make_cubemap("linear"), make_cubemap("uniform")]
	start = time.perf_counter()
	results["mixed"] = prefilter_cubemaps(tools, blobs, OUT_SIZE)
	results["mixed_seconds"] = time.perf_counter() - start
	results["random"] = prefilter_cubemaps(tools, [make_cubemap("random", seed=36)], OUT_SIZE)
	blobs = [make_cubemap("uniform", LARGE_SIZE), make_cubemap("faces", LARGE_SIZE)]
	start = time.perf_counter()
	results["large"] = prefilter_cubemaps(tools, blobs, LARGE_SIZE)
	results["large_seconds"] = time.perf_counter() - start
	del blobs
	work_dir = tempfile.mkdtemp(prefix="specular_import_")
	try:
		results["import"] = import_like_editor(tools, work_dir)
	finally:
		shutil.rmtree(work_dir, ignore_errors=True)

	d3d11 = envmap.module_path("d3d11.dll")
	if mode == "cpu":
		assert d3d11 and d3d11.lower() == fake.lower(), f"CPU版の確認用のd3d11.dllが使われていない: {d3d11}"
	else:
		assert d3d11 and "system32" in d3d11.lower(), f"GPU版でD3D11が読み込まれていない: {d3d11}"

	with open(out_path, "wb") as f:
		pickle.dump(results, f)

	if mode == "cpu":
		shutil.rmtree(fake_dir, ignore_errors=True)


def run(dll_path, mode, work_dir):
	out_path = os.path.join(work_dir, f"{mode}.pickle")
	result = subprocess.run([sys.executable, os.path.abspath(__file__), "--child", mode, "--dll", dll_path, "--out", out_path],
							capture_output=True, text=True, timeout=1200)
	if result.returncode != 0:
		raise RuntimeError(f"{mode}版の実行に失敗しました (exit {result.returncode})\n{result.stdout}{result.stderr}")
	with open(out_path, "rb") as f:
		results = pickle.load(f)
	for case in ("mixed", "random", "large", "import"):
		results[case] = parse_images(results[case])
	return results


def radical_inverse_vdc(i):
	return int(f"{i:032b}"[::-1], 2) / 4294967296.0


def specular_half_vectors(roughness):
	"""シェーダのImportanceSampleGGX(Hammersley(i, N), roughness^4)と同じ接空間のハーフベクトル(倍精度)"""
	a = roughness ** 4
	vectors = []
	for i in range(SAMPLE_COUNT):
		phi = 2.0 * math.pi * i / SAMPLE_COUNT
		e = radical_inverse_vdc(i)
		cos_theta = math.sqrt((1.0 - e) / (1.0 + (a - 1.0) * e))
		sin_theta = math.sqrt(max(1.0 - cos_theta * cos_theta, 0.0))
		vectors.append((sin_theta * math.cos(phi), sin_theta * math.sin(phi), cos_theta))
	return vectors


def cross(a, b):
	return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def normalize(v):
	length = math.sqrt(sum(c * c for c in v))
	return tuple(c / length for c in v)


def expected_mean_direction(n, half_vectors):
	"""N = V = nのとき、NoLで重み付けたLの平均 Σ NoL_i L_i / Σ NoL_i(シェーダのPrefilterEnvMapと同じ方向の集合)"""
	up = (0.0, 0.0, 1.0) if abs(n[2]) < 0.999 else (1.0, 0.0, 0.0)
	tx = normalize(cross(up, n))
	ty = cross(n, tx)
	acc = [0.0, 0.0, 0.0]
	total = 0.0
	for hx, hy, hz in half_vectors:
		h = tuple(hx * tx[k] + hy * ty[k] + hz * n[k] for k in range(3))
		v_dot_h = sum(n[k] * h[k] for k in range(3))
		l = tuple(2.0 * v_dot_h * h[k] - n[k] for k in range(3))
		n_dot_l = min(max(sum(n[k] * l[k] for k in range(3)), 0.0), 1.0)
		if n_dot_l > 0.0:
			for k in range(3):
				acc[k] += n_dot_l * l[k]
			total += n_dot_l
	return tuple(c / total for c in acc)


def check_linear(images):
	"""1次関数の入力の結果のうち、各ミップの各面でLINEAR_CHECK_GRID^2個の画素を、倍精度で求めた値と比べ、最大の絶対誤差を返す"""
	worst = 0.0
	for mip in range(1, OUT_MIPS):
		size = OUT_SIZE >> mip
		half_vectors = specular_half_vectors(mip / (OUT_MIPS - 1))
		step = max(size // LINEAR_CHECK_GRID, 1)
		for face in range(6):
			width, height, values = image_at(images, 0, face, mip)
			assert (width, height) == (size, size)
			for y in range(step // 2, size, step):
				for x in range(step // 2, size, step):
					mean = expected_mean_direction(cube_direction(face, x, y, size), half_vectors)
					i = (y * size + x) * 4
					for k in range(3):
						worst = max(worst, abs(values[i + k] - (LINEAR_A + LINEAR_B * mean[k])))
	return worst


def max_error_constant(images, cubemap, color_of_face, relative):
	"""cubemap番目のキューブマップの全ミップの全画素について、color_of_face(face)との最大誤差"""
	worst = 0.0
	for face in range(6):
		expected = color_of_face(face)
		for mip in range(OUT_MIPS):
			_, _, values = image_at(images, cubemap, face, mip)
			for k in range(3):
				e = expected[k]
				channel = values[k::4]
				worst = max(worst, max(abs(v - e) for v in channel) / (abs(e) if relative else 1.0))
	return worst


def input_mip0_matches(images, cubemap, blob):
	"""出力のミップ0が入力(build_cubemap_blob())と完全に一致するか"""
	inputs = parse_images(blob)
	return all(image_at(images, cubemap, face, 0)[2] == inputs[face][2] for face in range(6))


def variance(values):
	rgb = [v for i, v in enumerate(values) if i % 4 != 3]
	mean = sum(rgb) / len(rgb)
	return sum((v - mean) ** 2 for v in rgb) / len(rgb)


def check(label, error, tolerance):
	passed = error <= tolerance
	print(f"  {label}: 最大誤差 {error:.3g} (許容値 {tolerance:g}) {'OK' if passed else 'NG'}")
	return passed


def check_ok(label, ok):
	print(f"  {label}: {'OK' if ok else 'NG'}")
	return ok


def check_known_answers(mode, results):
	print(f"{mode}版:")
	mixed = results["mixed"]
	passed = check_ok("ミップ0(ラフネス0)が入力と完全に一致(1次関数の入力)", input_mip0_matches(mixed, 0, make_cubemap("linear")))
	passed = check_ok("ミップ0(ラフネス0)が入力と完全に一致(乱数の模様の入力)",
					  input_mip0_matches(results["random"], 0, make_cubemap("random", seed=36))) and passed
	passed = check("1次関数の入力 -> a + b・(Σ NoL L / Σ NoL)", check_linear(mixed), LINEAR_TOLERANCE) and passed
	passed = check("一様な色の入力(2つ目のキューブマップ) -> すべてのミップが同じ色",
				   max_error_constant(mixed, 1, lambda f: UNIFORM_COLOR, True), UNIFORM_TOLERANCE) and passed

	random_images = results["random"]
	variances = [sum(variance(image_at(random_images, 0, face, mip)[2]) for face in range(6)) / 6 for mip in range(OUT_MIPS)]
	decreasing = all(a > b for a, b in zip(variances, variances[1:]))
	passed = check_ok("ラフネスが大きいミップほどぼける(乱数の模様の分散: " +
					  ", ".join(f"{v:.3g}" for v in variances) + ")", decreasing) and passed

	import_color = envmap.from_rgbe(envmap.to_rgbe(IMPORT_COLOR))
	passed = check("Import -> PrefilterSpecularIBL -> BC6H -> Decompress(一様な色)",
				   max_error_constant(results["import"], 0, lambda f: import_color, True), BC6H_TOLERANCE) and passed
	passed = check(f"{LARGE_SIZE}の6面の一様な色の入力 -> すべてのミップが同じ色",
				   max_error_constant(results["large"], 0, lambda f: UNIFORM_COLOR, True), UNIFORM_TOLERANCE) and passed
	seconds = results["large_seconds"]
	time_ok = seconds <= LARGE_TIME_LIMIT
	print(f"  {LARGE_SIZE}の6面のキューブマップ2つの処理時間: {seconds:.2f}秒 (上限 {LARGE_TIME_LIMIT:g}秒) {'OK' if time_ok else 'NG'}")
	print(f"  (参考) {OUT_SIZE}の6面のキューブマップ2つの処理時間: {results['mixed_seconds']:.2f}秒")
	passed = passed and time_ok
	alpha_ok = all(v == 1.0 for images in (mixed, random_images) for _, _, values in images for v in values[3::4])
	return check_ok("出力のアルファがすべて1", alpha_ok) and passed


def check_cpu_gpu(cpu, gpu):
	print("CPU版とGPU版の比較:")
	passed = True
	for case in ("mixed", "random", "large"):
		worst = 0.0
		identical = True
		for (_, _, c_values), (_, _, g_values) in zip(cpu[case], gpu[case]):
			identical = identical and c_values == g_values
			for i, (cv, gv) in enumerate(zip(c_values, g_values)):
				if i % 4 != 3:
					worst = max(worst, abs(cv - gv) / max(abs(cv), 1e-6))
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

	work_dir = tempfile.mkdtemp(prefix="specular_cpu_gpu_test_")
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
