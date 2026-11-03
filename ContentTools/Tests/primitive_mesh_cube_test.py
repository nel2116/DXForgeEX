# 立方体のプリミティブメッシュ(create_cube, Primal Day 075, Issue #32)を確かめる回帰テスト
#
# ContentTools.dllのCreatePrimitiveMesh()をctypesで直接呼ぶ。GUIもGPUもFBX SDKも不要。
#   python ContentTools/Tests/primitive_mesh_cube_test.py                       # x64/ReleaseEditor/ContentTools.dll
#   python ContentTools/Tests/primitive_mesh_cube_test.py --config DebugEditor  # x64/DebugEditor/ContentTools.dll
#   python ContentTools/Tests/primitive_mesh_cube_test.py --dll <ContentTools.dllのパス>
# すべて合格すれば終了コード0、不合格があれば1。
#
# 設定は、エディタのPrimitiveMeshDialogと既定のジオメトリ(DefaultAssets.DefaultGeometry)と同じ
# GeometryImportSettingsの既定値(スムージング角度178度、接線を計算する)。
# 確かめること(分割数と大きさの組ごとに):
#   1. LODが1つ("cube")、メッシュが1つで、要素の種類が static_normal_texture
#   2. 頂点数とインデックス数が、面ごとに (x+1)(y+1) 頂点、6xy インデックス(分割数は1〜10に丸める)
#   3. 頂点の(位置, UV)の組が、Primal Day 075 の create_cube の式をこのテストで計算したものと一致する
#   4. すべての三角形が外向き(位置から求めた面の向きと、出力の法線が、どちらも立方体の外を向く)
#   5. 位置が -size〜size の範囲にあり、8つの角がすべてある
import argparse
import ctypes
import os
import struct
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))

PRIMITIVE_CUBE = 1
ELEMENTS_STATIC_NORMAL_TEXTURE = 0x03
STATIC_NORMAL_TEXTURE_SIZE = 20  # u8 color[3], u8 t_sign, u16 normal[2], u16 tangent[2], v2 uv
POSITION_TOLERANCE = 1e-5
UV_TOLERANCE = 1e-5
# 法線は16ビットに詰めてあるので、その丸めの分
NORMAL_TOLERANCE = 1e-3


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


class PrimitiveInitInfo(ctypes.Structure):
	_fields_ = [
		("type", ctypes.c_uint32),
		("segments", ctypes.c_uint32 * 3),
		("size", ctypes.c_float * 3),
		("lod", ctypes.c_uint32),
	]


def suppress_debug_crt_dialogs():
	# Debugビルドのassertはモーダルのダイアログを出して止まるので、標準エラー出力に流す。
	try:
		ucrt = ctypes.CDLL("ucrtbased.dll")
	except OSError:
		return
	ucrt._set_error_mode(1)  # _OUT_TO_STDERR
	ucrt._set_abort_behavior(0, 3)  # _WRITE_ABORT_MSG | _CALL_REPORTFAULT


class Reader:
	def __init__(self, blob):
		self.blob = blob
		self.offset = 0

	def read(self, fmt):
		values = struct.unpack_from("<" + fmt, self.blob, self.offset)
		self.offset += struct.calcsize("<" + fmt)
		return values

	def u32(self):
		return self.read("I")[0]

	def string(self):
		size = self.u32()
		value = self.blob[self.offset:self.offset + size].decode("utf-8")
		self.offset += size
		return value


def create_cube(tools, segments, size):
	data = SceneData()
	data.settings = GeometryImportSettings(178.0, 0, 1, 0, 1, 1, 0)  # GeometryImportSettings(C#)の既定値
	info = PrimitiveInitInfo(PRIMITIVE_CUBE, (ctypes.c_uint32 * 3)(*segments), (ctypes.c_float * 3)(*size), 0)
	tools.CreatePrimitiveMesh(ctypes.byref(data), ctypes.byref(info))
	assert data.buffer and data.buffer_size > 0, "CreatePrimitiveMesh がデータを返さない"
	try:
		blob = ctypes.string_at(data.buffer, data.buffer_size)
	finally:
		ctypes.windll.ole32.CoTaskMemFree(ctypes.c_void_p(data.buffer))

	r = Reader(blob)
	scene = {"name": r.string(), "lods": []}
	for _ in range(r.u32()):
		lod = {"name": r.string(), "meshes": []}
		for _ in range(r.u32()):
			mesh = {"name": r.string()}
			mesh["lod_id"], elements_size, mesh["elements_type"], num_vertices, index_size, num_indices = r.read("6I")
			mesh["lod_threshold"] = r.read("f")[0]
			mesh["positions"] = [r.read("3f") for _ in range(num_vertices)]
			mesh["elements"] = []
			for _ in range(num_vertices):
				element = blob[r.offset:r.offset + elements_size]
				r.offset += elements_size
				mesh["elements"].append(element)
			mesh["elements_size"] = elements_size
			mesh["indices"] = list(r.read(("H" if index_size == 2 else "I") * num_indices))
			lod["meshes"].append(mesh)
		scene["lods"].append(lod)
	assert r.offset == len(blob), f"読み残しがある: {r.offset} / {len(blob)}"
	return scene


def clamp_segments(value):
	return min(max(value, 1), 10)


# Primal Day 075 の create_cube(ContentTools/PrimitiveMesh.cpp)の式。面ごとの軸、UVの左上、面の上の頂点。
AXES = [(2, 1), (0, 2), (0, 1)]
U_RANGE = [0.0, 0.5, 0.25, 0.25, 0.25, 0.75]
V_RANGE = [0.375, 0.375, 0.125, 0.625, 0.375, 0.375]
FACE_NORMALS = [(-1, 0, 0), (1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)]


def face_vertex(face, x, y):
	return [(-1.0, -y, x), (1.0, -y, -x), (x, 1.0, y), (x, -1.0, -y), (x, -y, 1.0), (-x, -y, -1.0)][face]


def expected_cube(segments, size):
	"""面ごとの [(位置, UV)] と、面ごとのインデックス数"""
	faces = []
	for face in range(6):
		axis = AXES[face >> 1]
		x_count = clamp_segments(segments[axis[0]])
		y_count = clamp_segments(segments[axis[1]])
		vertices = []
		for y in range(y_count + 1):
			for x in range(x_count + 1):
				p = face_vertex(face, 2.0 * x / x_count - 1.0, 2.0 * y / y_count - 1.0)
				position = (p[0] * size[0], p[1] * size[1], p[2] * size[2])
				uv = (U_RANGE[face] + x * 0.25 / x_count, 1.0 - V_RANGE[face] - y * 0.25 / y_count)
				vertices.append((position, uv))
		faces.append((vertices, 6 * x_count * y_count))
	return faces


def unpack_normal(element):
	t_sign = element[3]
	nx, ny = struct.unpack_from("<2H", element, 4)
	nx = nx / 65535.0 * 2.0 - 1.0
	ny = ny / 65535.0 * 2.0 - 1.0
	nz = max(0.0, 1.0 - nx * nx - ny * ny) ** 0.5 * (1.0 if t_sign & 4 else -1.0)
	return (nx, ny, nz)


def close(a, b, tolerance):
	return all(abs(x - y) <= tolerance for x, y in zip(a, b))


def check(tools, segments, size):
	label = f"segments={segments} size={size}"
	errors = []
	scene = create_cube(tools, segments, size)

	if len(scene["lods"]) != 1 or scene["lods"][0]["name"] != "cube" or len(scene["lods"][0]["meshes"]) != 1:
		errors.append(f"LOD/メッシュの数か名前が違う: {[(l['name'], len(l['meshes'])) for l in scene['lods']]}")
		return label, errors
	mesh = scene["lods"][0]["meshes"][0]
	if mesh["elements_type"] != ELEMENTS_STATIC_NORMAL_TEXTURE or mesh["elements_size"] != STATIC_NORMAL_TEXTURE_SIZE:
		errors.append(f"要素の種類が違う: type={mesh['elements_type']} size={mesh['elements_size']}")
		return label, errors

	faces = expected_cube(segments, size)
	expected_vertices = sum(len(v) for v, _ in faces)
	expected_indices = sum(n for _, n in faces)
	positions = mesh["positions"]
	indices = mesh["indices"]
	if len(positions) != expected_vertices or len(indices) != expected_indices:
		errors.append(f"頂点数/インデックス数: {len(positions)}/{len(indices)}(期待値 {expected_vertices}/{expected_indices})")

	# 3. (位置, UV) の組が一致する
	actual = []
	for position, element in zip(positions, mesh["elements"]):
		uv = struct.unpack_from("<2f", element, 12)
		actual.append((position, uv))
	unmatched = list(actual)
	for face_vertices, _ in faces:
		for position, uv in face_vertices:
			for i, (p, t) in enumerate(unmatched):
				if close(p, position, POSITION_TOLERANCE) and close(t, uv, UV_TOLERANCE):
					del unmatched[i]
					break
			else:
				errors.append(f"頂点がない: position={position} uv={uv}")
	if unmatched:
		errors.append(f"余分な頂点が {len(unmatched)} 個ある: {unmatched[:3]}")

	# 4. 三角形の向きと法線が外向き
	for t in range(0, len(indices) - 2, 3):
		p0, p1, p2 = (positions[indices[t + k]] for k in range(3))
		e1 = [p1[k] - p0[k] for k in range(3)]
		e2 = [p2[k] - p0[k] for k in range(3)]
		# create_plane(Y+ を向く)と同じ巻き方なら、cross(p1 - p0, p2 - p0) が面の外を向く
		n = (e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0])
		center = [(p0[k] + p1[k] + p2[k]) / 3.0 for k in range(3)]
		if sum(n[k] * center[k] for k in range(3)) <= 0.0:
			errors.append(f"三角形 {t // 3} が内向き: {p0} {p1} {p2}")
			break
	for position, element in zip(positions, mesh["elements"]):
		normal = unpack_normal(element)
		# 位置がいちばん外にある軸の向き(面の法線)と一致する
		ratios = [abs(position[k]) / size[k] for k in range(3)]
		axis = max(range(3), key=lambda k: ratios[k])
		face_normal = [0.0, 0.0, 0.0]
		face_normal[axis] = 1.0 if position[axis] > 0 else -1.0
		if not close(normal, face_normal, NORMAL_TOLERANCE) and not any(
				abs(ratios[k] - ratios[axis]) < 1e-6 for k in range(3) if k != axis):
			errors.append(f"法線が面の向きと違う: position={position} normal={normal}")
			break

	# 5. 範囲と角
	for position in positions:
		if any(abs(position[k]) > size[k] + POSITION_TOLERANCE for k in range(3)):
			errors.append(f"位置が範囲の外: {position}")
			break
	for sx in (-1, 1):
		for sy in (-1, 1):
			for sz in (-1, 1):
				corner = (sx * size[0], sy * size[1], sz * size[2])
				if not any(close(p, corner, POSITION_TOLERANCE) for p in positions):
					errors.append(f"角がない: {corner}")

	return label, errors


CASES = [
	((1, 1, 1), (1.0, 1.0, 1.0)),  # 既定のジオメトリ(DefaultCube.asset)と同じ
	((2, 3, 4), (1.0, 1.0, 1.0)),
	((1, 1, 1), (0.5, 2.0, 3.0)),
	((20, 0, 5), (1.0, 1.0, 1.0)),  # 分割数は 1〜10 に丸める
]


def main():
	parser = argparse.ArgumentParser(description=__doc__)
	parser.add_argument("--config", default="ReleaseEditor")
	parser.add_argument("--dll")
	args = parser.parse_args()

	dll_path = os.path.abspath(args.dll or os.path.join(REPO_ROOT, "x64", args.config, "ContentTools.dll"))
	print(f"ContentTools.dll: {dll_path}")
	if not os.path.exists(dll_path):
		print("FAILED: ContentTools.dll がない")
		return 1

	tools = ctypes.CDLL(dll_path)
	suppress_debug_crt_dialogs()

	passed = True
	for segments, size in CASES:
		label, errors = check(tools, segments, size)
		if errors:
			passed = False
			print(f"FAIL {label}")
			for error in errors[:10]:
				print(f"  {error}")
		else:
			print(f"PASS {label}")

	print("ALL PASSED" if passed else "FAILED")
	return 0 if passed else 1


if __name__ == "__main__":
	sys.exit(main())
