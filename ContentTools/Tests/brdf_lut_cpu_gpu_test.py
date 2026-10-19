# 鏡面反射のIBL用のBRDF LUT(split sum approximationの後半, Issue #37)のCPU版とGPU版を確かめる回帰テスト
#
# ContentTools.dllのComputeBrdfIntegrationLut()をctypesで直接呼ぶ。GUIもFBX SDKも不要(D3D11を使えるGPUは必要)。
#   python ContentTools/Tests/brdf_lut_cpu_gpu_test.py                       # x64/ReleaseEditor/ContentTools.dll
#   python ContentTools/Tests/brdf_lut_cpu_gpu_test.py --config DebugEditor  # x64/DebugEditor/ContentTools.dll
#   python ContentTools/Tests/brdf_lut_cpu_gpu_test.py --dll <ContentTools.dllのパス>
# すべて合格すれば終了コード0、不合格があれば1、
# ハードウェアのD3D11デバイスを作れずGPU版を試せない環境では"SKIPPED"と表示して2を返す。
#
# CPU版を使わせる方法(D3D11CreateDeviceを持たない"d3d11.dll"を先に読み込む)は envmap_cpu_gpu_test.py と同じ。
#
# 出力は256x256のR16G16_FLOATの2Dテクスチャ。画素(x, y)は NoV = (x + 0.5) / 256、ラフネス = (y + 0.5) / 256
# (画素の中心)での、鏡面反射のBRDFとNoLの積の半球上の積分をF0のスケール(r)とバイアス(g)に分けたもの。
# 確かめること(CPU版とGPU版のそれぞれ):
#   1. 出力の形式(256x256、ミップ1つ、配列1つ、R16G16_FLOAT、キューブマップでない)
#   2. 答えがわかる値
#      - ラフネス0の端(y = 0, ラフネス 0.5 / 256): GGXの分布はほぼデルタ関数で、すべてのサンプルが H = N になるので
#        (r, g) = (1 - Fc, Fc)、Fc = (1 - NoV)^5。NoV = 1の端(x = 255)では (1, 0) に近い。
#      - 重点サンプリングを使わない数値積分(ハーフベクトルの方向の細かい格子での中点則)で求めた値と、いくつかの点で一致する。
#        シェーダの式(pdfで割った重み)と独立に、積分そのものを確かめる。
#      - エネルギー保存: r + g <= 1(単一散乱なので、ラフネスが大きいほど1より小さくなる)
#   3. シェーダと同じ式(Hammersleyの1024サンプル)を倍精度で計算した値と、8x8の格子の画素で一致する。
#   4. CPU版とGPU版が画素ごとに誤差の範囲で一致する。
import argparse
import ctypes
import math
import os
import pickle
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

LUT_SIZE = 256
SAMPLE_COUNT = 1024
DXGI_FORMAT_R16G16_FLOAT = 34
FLAG_IS_CUBE_MAP = 0x10
# 2. ラフネス0の端の値(絶対誤差)。R16G16_FLOATの丸め(1付近で半精度の1目盛りの半分 2.4e-4)と浮動小数点の丸め誤差。
EDGE_TOLERANCE = 1e-3
# 2. 数値積分との差(絶対誤差)。1024サンプルの重点サンプリングの誤差(ラフネスが小さいほど小さい)。
QUADRATURE_TOLERANCE = 0.01
# 数値積分で確かめる点(NoV, ラフネス)。それぞれ最も近い画素の中心の値で比べる。
QUADRATURE_POINTS = [(0.1, 0.25), (0.5, 0.5), (0.9, 0.75), (0.3, 0.95), (0.95, 0.15)]
QUADRATURE_THETA_STEPS = 1500
QUADRATURE_PHI_STEPS = 120
# 2. r + g <= 1 の許容値
ENERGY_TOLERANCE = 2e-3
# 3. 倍精度で求めたシェーダと同じ和との差(絶対誤差)。R16G16_FLOATの丸めと、単精度の和の丸め誤差。
REFERENCE_TOLERANCE = 1e-3
REFERENCE_GRID = 8
# 4. CPU版とGPU版の差(絶対誤差)。NoVが0に近くラフネスが小さい画素(x, y < 16程度)では、NoL = 2 VoH NoH - NoVの桁落ちで、
#    地平線すれすれのサンプルが入るかどうかが丸め誤差で変わり、1サンプルの重みが大きいので、GPU版(単精度)の値が
#    倍精度の和から最大1.5e-3ずれる(CPU版はハーフベクトルから倍精度で求めるので、倍精度の和との差は1e-4程度)。
#    それ以外の画素の差はR16G16_FLOATの丸め(5e-4以下)。
CPU_GPU_TOLERANCE = 2e-3


def half_to_float(bits):
	return struct.unpack("<e", struct.pack("<H", bits))[0]


def compute_lut(tools):
	"""ComputeBrdfIntegrationLut()を呼び、(info, [y][x] = (r, g))を返す"""
	data = TextureData()
	tools.ComputeBrdfIntegrationLut(ctypes.byref(data))
	info = data.info
	try:
		assert info.import_error == 0, f"import_error={info.import_error}"
		blob = ctypes.string_at(data.subresource_data, data.subresource_size)
	finally:
		irradiance.free_texture_data(data)

	width, height, row_pitch, slice_pitch = struct.unpack_from("<4I", blob, 0)
	assert (width, height) == (info.width, info.height), f"subresource={width}x{height} info={info.width}x{info.height}"
	assert len(blob) == 16 + slice_pitch, f"サブリソースが1つではない: {len(blob)} bytes"
	rows = []
	for y in range(height):
		halves = struct.unpack_from(f"<{width * 2}H", blob, 16 + y * row_pitch)
		rows.append([(half_to_float(halves[2 * x]), half_to_float(halves[2 * x + 1])) for x in range(width)])
	return {"width": info.width, "height": info.height, "array_size": info.array_size, "mip_levels": info.mip_levels,
			"format": info.format, "flags": info.flags, "rows": rows}


def run_child(dll_path, mode, out_path):
	"""子プロセスで実行する。結果をout_pathに書く。"""
	if mode == "cpu":
		fake_dir = tempfile.mkdtemp(prefix="brdf_lut_fake_d3d11_")
		fake = os.path.join(fake_dir, "d3d11.dll")
		shutil.copyfile(os.path.join(os.environ["SystemRoot"], "System32", "version.dll"), fake)
		ctypes.WinDLL(fake)

	ctypes.WinDLL("ole32").CoInitializeEx(None, 2)  # COINIT_APARTMENTTHREADED (他のテストと同じ)
	tools = ctypes.CDLL(dll_path)
	envmap.suppress_debug_crt_dialogs()

	start = time.perf_counter()
	result = compute_lut(tools)
	result["seconds"] = time.perf_counter() - start

	d3d11 = envmap.module_path("d3d11.dll")
	if mode == "cpu":
		assert d3d11 and d3d11.lower() == fake.lower(), f"CPU版の確認用のd3d11.dllが使われていない: {d3d11}"
	else:
		assert d3d11 and "system32" in d3d11.lower(), f"GPU版でD3D11が読み込まれていない: {d3d11}"

	with open(out_path, "wb") as f:
		pickle.dump(result, f)

	if mode == "cpu":
		shutil.rmtree(fake_dir, ignore_errors=True)


def run(dll_path, mode, work_dir):
	out_path = os.path.join(work_dir, f"{mode}.pickle")
	result = subprocess.run([sys.executable, os.path.abspath(__file__), "--child", mode, "--dll", dll_path, "--out", out_path],
							capture_output=True, text=True, timeout=600)
	if result.returncode != 0:
		raise RuntimeError(f"{mode}版の実行に失敗しました (exit {result.returncode})\n{result.stdout}{result.stderr}")
	with open(out_path, "rb") as f:
		return pickle.load(f)


def texel_center(i):
	return (i + 0.5) / LUT_SIZE


def v_smith_ggx_correlated(n_dot_v, n_dot_l, a):
	ggx_l = n_dot_v * math.sqrt((-n_dot_l * a + n_dot_l) * n_dot_l + a)
	ggx_v = n_dot_l * math.sqrt((-n_dot_v * a + n_dot_v) * n_dot_v + a)
	return 0.5 / (ggx_v + ggx_l)


def reference_integrate_brdf(n_dot_v, roughness):
	"""シェーダのIntegrateBRDF()と同じ和(Hammersleyの1024サンプル)を倍精度で求める"""
	a = roughness ** 4
	v = (math.sqrt(1.0 - n_dot_v * n_dot_v), 0.0, n_dot_v)
	sum_a = sum_b = 0.0
	for i in range(SAMPLE_COUNT):
		phi = 2.0 * math.pi * i / SAMPLE_COUNT
		e = int(f"{i:032b}"[::-1], 2) / 4294967296.0
		cos_theta = math.sqrt((1.0 - e) / (1.0 + (a - 1.0) * e))
		sin_theta = math.sqrt(max(1.0 - cos_theta * cos_theta, 0.0))
		h = (sin_theta * math.cos(phi), sin_theta * math.sin(phi), cos_theta)
		v_dot_h = v[0] * h[0] + v[1] * h[1] + v[2] * h[2]
		n_dot_l = min(max(2.0 * v_dot_h * h[2] - v[2], 0.0), 1.0)
		v_dot_h = min(max(v_dot_h, 0.0), 1.0)
		if n_dot_l > 0.0:
			g_vis = 4.0 * n_dot_l * v_smith_ggx_correlated(n_dot_v, n_dot_l, a) * v_dot_h / h[2]
			fc = (1.0 - v_dot_h) ** 5
			sum_a += (1.0 - fc) * g_vis
			sum_b += fc * g_vis
	return sum_a / SAMPLE_COUNT, sum_b / SAMPLE_COUNT


def quadrature_integrate_brdf(n_dot_v, roughness):
	"""∫ D * Vis * NoL * (1 - Fc, Fc) dω_L を、重点サンプリングを使わずに数値積分する。
	ハーフベクトルHの(θ, φ)の格子の中点則で、dω_L = 4 VoH dω_H、dω_H = sinθ dθ dφ。
	GGXの分布はθが小さいところに集中するので、θ = atan(alpha * tan(t))(t は [0, π/2) で等間隔)と変数変換する。
	Vはxz平面上にあり、被積分関数はφについて対称なので、φは[0, π]で積分して2倍する。"""
	a = roughness ** 4
	alpha = roughness * roughness
	v = (math.sqrt(1.0 - n_dot_v * n_dot_v), 0.0, n_dot_v)
	dt = (0.5 * math.pi) / QUADRATURE_THETA_STEPS
	dphi = math.pi / QUADRATURE_PHI_STEPS
	cos_phis = [math.cos((j + 0.5) * dphi) for j in range(QUADRATURE_PHI_STEPS)]
	sum_a = sum_b = 0.0
	for i in range(QUADRATURE_THETA_STEPS):
		t = (i + 0.5) * dt
		theta = math.atan(alpha * math.tan(t))
		# dθ/dt
		d_theta = alpha / (math.cos(t) ** 2 + (alpha * math.sin(t)) ** 2)
		sin_theta = math.sin(theta)
		cos_theta = math.cos(theta)
		d = (cos_theta * a - cos_theta) * cos_theta + 1.0
		ggx_d = a / (math.pi * d * d)
		weight_theta = ggx_d * sin_theta * d_theta * dt * dphi * 2.0
		for cos_phi in cos_phis:
			v_dot_h = v[0] * sin_theta * cos_phi + v[2] * cos_theta
			if v_dot_h <= 0.0:
				continue
			n_dot_l = 2.0 * v_dot_h * cos_theta - v[2]
			if n_dot_l <= 0.0:
				continue
			f = v_smith_ggx_correlated(n_dot_v, n_dot_l, a) * n_dot_l * 4.0 * v_dot_h * weight_theta
			fc = (1.0 - v_dot_h) ** 5
			sum_a += (1.0 - fc) * f
			sum_b += fc * f
	return sum_a, sum_b


def check(label, error, tolerance):
	passed = error <= tolerance
	print(f"  {label}: 最大誤差 {error:.3g} (許容値 {tolerance:g}) {'OK' if passed else 'NG'}")
	return passed


def check_ok(label, ok):
	print(f"  {label}: {'OK' if ok else 'NG'}")
	return ok


def check_known_answers(mode, result, quadrature, reference):
	print(f"{mode}版:")
	shape = (result["width"], result["height"], result["array_size"], result["mip_levels"], result["format"])
	passed = check_ok(f"出力の形式 {result['width']}x{result['height']}、配列{result['array_size']}、ミップ{result['mip_levels']}、"
					  f"フォーマット{result['format']}", shape == (LUT_SIZE, LUT_SIZE, 1, 1, DXGI_FORMAT_R16G16_FLOAT))
	passed = check_ok("キューブマップでない", not (result["flags"] & FLAG_IS_CUBE_MAP)) and passed
	rows = result["rows"]

	worst = 0.0
	for x in range(LUT_SIZE):
		fc = (1.0 - texel_center(x)) ** 5
		r, g = rows[0][x]
		worst = max(worst, abs(r - (1.0 - fc)), abs(g - fc))
	passed = check("ラフネス0の端 -> (1 - Fc, Fc), Fc = (1 - NoV)^5", worst, EDGE_TOLERANCE) and passed
	r, g = rows[0][LUT_SIZE - 1]
	print(f"    (参考) NoV = 1, ラフネス0の端の画素: ({r:.5f}, {g:.3g})")

	worst = 0.0
	for (x, y), (qa, qb) in quadrature.items():
		r, g = rows[y][x]
		worst = max(worst, abs(r - qa), abs(g - qb))
	passed = check("重点サンプリングを使わない数値積分との差(" +
				   ", ".join(f"NoV {texel_center(x):.3f} / ラフネス {texel_center(y):.3f}" for x, y in quadrature) + ")",
				   worst, QUADRATURE_TOLERANCE) and passed

	worst = max(r + g for row in rows for r, g in row) - 1.0
	passed = check("エネルギー保存 r + g <= 1(1を超えた量)", max(worst, 0.0), ENERGY_TOLERANCE) and passed
	print(f"    (参考) NoV = 0.5付近の r + g: ラフネス0付近 {sum(rows[0][LUT_SIZE // 2]):.4f}、"
		  f"0.5付近 {sum(rows[LUT_SIZE // 2][LUT_SIZE // 2]):.4f}、1付近 {sum(rows[LUT_SIZE - 1][LUT_SIZE // 2]):.4f}")

	worst = 0.0
	for (x, y), (ra, rb) in reference.items():
		r, g = rows[y][x]
		worst = max(worst, abs(r - ra), abs(g - rb))
	passed = check(f"シェーダと同じ和を倍精度で求めた値との差({REFERENCE_GRID}x{REFERENCE_GRID}の画素)", worst, REFERENCE_TOLERANCE) and passed

	print(f"  処理時間: {result['seconds']:.2f}秒")
	return passed


def check_cpu_gpu(cpu, gpu):
	print("CPU版とGPU版の比較:")
	worst = 0.0
	different = 0
	for c_row, g_row in zip(cpu["rows"], gpu["rows"]):
		for c, g in zip(c_row, g_row):
			if c != g:
				different += 1
			worst = max(worst, abs(c[0] - g[0]), abs(c[1] - g[1]))
	print(f"  値が異なる画素の数: {different} / {LUT_SIZE * LUT_SIZE}")
	return check("全画素", worst, CPU_GPU_TOLERANCE)


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

	work_dir = tempfile.mkdtemp(prefix="brdf_lut_cpu_gpu_test_")
	try:
		gpu = run(dll_path, "gpu", work_dir)
		cpu = run(dll_path, "cpu", work_dir)
	finally:
		shutil.rmtree(work_dir, ignore_errors=True)

	quadrature = {}
	for n_dot_v, roughness in QUADRATURE_POINTS:
		x = min(int(n_dot_v * LUT_SIZE), LUT_SIZE - 1)
		y = min(int(roughness * LUT_SIZE), LUT_SIZE - 1)
		quadrature[(x, y)] = quadrature_integrate_brdf(texel_center(x), texel_center(y))
	step = LUT_SIZE // REFERENCE_GRID
	# 端の画素(0と255)も含める
	indices = sorted({*range(0, LUT_SIZE, step), LUT_SIZE - 1})
	reference = {(x, y): reference_integrate_brdf(texel_center(x), texel_center(y)) for y in indices for x in indices}

	passed = check_known_answers("GPU", gpu, quadrature, reference)
	passed = check_known_answers("CPU", cpu, quadrature, reference) and passed
	passed = check_cpu_gpu(cpu, gpu) and passed

	print("PASSED" if passed else "FAILED")
	return 0 if passed else 1


if __name__ == "__main__":
	sys.exit(main())
