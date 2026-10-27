# エディタ側のシェーダのコンパイル(Primal Day 076, Issue #29)の回帰テスト
#
# DXForgeEngineDLL.dllのCompileShader() / AddShaderGroup() / RemoveShaderGroup()をctypesで直接呼ぶ。
# エディタのDllWrappers(EngineAPI.cs)と同じ構造体・同じ引数の形式を使う。GUIもGPUも不要。
#   python DXForgeEngineDLL/Tests/shader_compilation_test.py                       # x64/DebugEditor/DXForgeEngineDLL.dll
#   python DXForgeEngineDLL/Tests/shader_compilation_test.py --config ReleaseEditor
#   python DXForgeEngineDLL/Tests/shader_compilation_test.py --dll <DXForgeEngineDLL.dllのパス>
# すべて合格すれば終了コード0、不合格があれば1。
#
# 確かめること:
#   1. エディタの既定のマテリアル用シェーダ(DXForgeEditor/Resources/MaterialEditor/DefaultMaterialShaders.hlsl)が、
#      エディタと同じ引数(MainVS: ELEMENTS_TYPE=0/1/3、MainPS、エンジンのシェーダのフォルダを -I)でコンパイルできる。
#      バイトコード(DXBCのコンテナ)とハッシュと逆アセンブルが返り、エラーは空。
#   2. わざと壊したシェーダでは、FALSEが返り、DXCのエラーメッセージ(行番号付き)が返る。プロセスは落ちない。
#      存在しない関数名・存在しないインクルードも同じ。
#   3. 不正な引数(関数名やソースが無い、シェーダの種類が範囲外)では、FALSEが返り、バッファは返らない。
#   4. コンパイルしたシェーダをエディタと同じ形式(keys[count], {u64 長さ, ハッシュ, バイトコード}[count])に詰めて
#      AddShaderGroup() に渡すと有効なIDが返り、RemoveShaderGroup() で消せる。壊れたデータでは無効なIDが返る。
#
# NOTE: Debug(DebugEditor)のDLLのassertはモーダルなダイアログを出して止まるため、assertを標準エラーに出して
#       abortするようにしてから呼ぶ(ucrtbasedの_set_error_mode / _set_abort_behavior)。
import argparse
import ctypes
import os
import struct
import sys

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
DEFAULT_SHADERS = os.path.join(REPO_ROOT, "DXForgeEditor", "Resources", "MaterialEditor", "DefaultMaterialShaders.hlsl")
ENGINE_SHADERS_DIR = os.path.join(REPO_ROOT, "DXForgeEngine", "Graphics", "Direct3D12", "Shaders")

SHADER_TYPE_VERTEX = 0
SHADER_TYPE_PIXEL = 4
SHADER_TYPE_COUNT = 8
HASH_LENGTH = 16
INVALID_ID = 0xFFFFFFFF
# DXForgeEditor の ElementsType(ContentTools の elements::elements_type)
ELEMENTS_TYPES = [0x00, 0x01, 0x03]


class ShaderData(ctypes.Structure):
	# DXForgeEngineDLL/EngineAPI.cpp の shader_data、エディタの EngineAPIStructs.ShaderData と同じ並び
	_fields_ = [
		("type", ctypes.c_uint32),
		("code_size", ctypes.c_uint32),
		("byte_code_size", ctypes.c_uint32),
		("errors_size", ctypes.c_uint32),
		("assembly_size", ctypes.c_uint32),
		("hash_size", ctypes.c_uint32),
		("code", ctypes.c_void_p),
		("byte_code_error_assembly_hash", ctypes.c_void_p),
		("function_name", ctypes.c_char_p),
		("extra_args", ctypes.c_char_p),
	]


class ShaderGroupData(ctypes.Structure):
	_fields_ = [
		("type", ctypes.c_uint32),
		("count", ctypes.c_uint32),
		("data_size", ctypes.c_uint32),
		("data", ctypes.c_void_p),
	]


class Result:
	def __init__(self, ok, byte_code, errors, assembly, hash_):
		self.ok = ok
		self.byte_code = byte_code
		self.errors = errors
		self.assembly = assembly
		self.hash = hash_


failures = []


def check(condition, message):
	print(("  PASS " if condition else "  FAIL ") + message)
	if not condition:
		failures.append(message)


def load_dll(path):
	# Debugのassertでダイアログを出さない
	try:
		ucrt = ctypes.CDLL("ucrtbased.dll")
		ucrt._set_error_mode(1)  # _OUT_TO_STDERR
		ucrt._set_abort_behavior(0, 3)
	except OSError:
		pass
	dll = ctypes.CDLL(path)
	dll.CompileShader.argtypes = [ctypes.POINTER(ShaderData)]
	dll.CompileShader.restype = ctypes.c_uint32
	dll.AddShaderGroup.argtypes = [ctypes.POINTER(ShaderGroupData)]
	dll.AddShaderGroup.restype = ctypes.c_uint32
	dll.RemoveShaderGroup.argtypes = [ctypes.c_uint32]
	dll.RemoveShaderGroup.restype = None
	return dll


def compile_shader(dll, code, function, shader_type, extra_args):
	"""EngineAPI.CompileShader(ShaderGroup) と同じ手順で1つの変種をコンパイルする"""
	source = code.encode("utf-8")
	buffer = ctypes.create_string_buffer(source, len(source))
	data = ShaderData()
	data.type = shader_type
	data.code_size = len(source)
	data.code = ctypes.cast(buffer, ctypes.c_void_p)
	data.function_name = function.encode("utf-8") if function is not None else None
	data.extra_args = ";".join(extra_args).encode("utf-8")
	ok = dll.CompileShader(ctypes.byref(data))
	byte_code = errors = assembly = hash_ = b""
	if data.byte_code_error_assembly_hash:
		total = data.byte_code_size + data.errors_size + data.assembly_size + data.hash_size
		blob = ctypes.string_at(data.byte_code_error_assembly_hash, total)
		offset = 0
		byte_code = blob[offset:offset + data.byte_code_size]; offset += data.byte_code_size
		errors = blob[offset:offset + data.errors_size]; offset += data.errors_size
		assembly = blob[offset:offset + data.assembly_size]; offset += data.assembly_size
		hash_ = blob[offset:offset + data.hash_size]
		ctypes.windll.ole32.CoTaskMemFree(ctypes.c_void_p(data.byte_code_error_assembly_hash))
	return Result(bool(ok), byte_code, errors.decode("utf-8", "replace"), assembly.decode("utf-8", "replace"), hash_)


def pack_for_engine(keys, results):
	"""ShaderGroup.PackForEngine() と同じ形式"""
	out = b"".join(struct.pack("<I", k) for k in keys)
	for r in results:
		out += struct.pack("<Q", len(r.byte_code)) + r.hash + r.byte_code
	return out


def add_shader_group(dll, shader_type, count, packed):
	buffer = ctypes.create_string_buffer(packed, len(packed))
	data = ShaderGroupData(shader_type, count, len(packed), ctypes.cast(buffer, ctypes.c_void_p))
	return dll.AddShaderGroup(ctypes.byref(data))


def main():
	parser = argparse.ArgumentParser()
	parser.add_argument("--config", default="DebugEditor", choices=["DebugEditor", "ReleaseEditor"])
	parser.add_argument("--dll")
	args = parser.parse_args()
	dll_path = args.dll or os.path.join(REPO_ROOT, "x64", args.config, "DXForgeEngineDLL.dll")
	print(f"DLL: {dll_path}")
	dll = load_dll(dll_path)

	code = open(DEFAULT_SHADERS, encoding="utf-8").read()
	include = ["-I", ENGINE_SHADERS_DIR]

	print("1. Default material shaders")
	vertex_shaders = []
	for elements_type in ELEMENTS_TYPES:
		r = compile_shader(dll, code, "MainVS", SHADER_TYPE_VERTEX, ["-D", f"ELEMENTS_TYPE={elements_type}"] + include)
		check(r.ok and r.byte_code[:4] == b"DXBC" and len(r.hash) == HASH_LENGTH and any(r.hash) and r.assembly and not r.errors,
			f"MainVS ELEMENTS_TYPE={elements_type}: {len(r.byte_code)} bytes, errors={r.errors!r}")
		vertex_shaders.append(r)
	pixel_shader = compile_shader(dll, code, "MainPS", SHADER_TYPE_PIXEL, include)
	check(pixel_shader.ok and pixel_shader.byte_code[:4] == b"DXBC" and len(pixel_shader.hash) == HASH_LENGTH and not pixel_shader.errors,
		f"MainPS: {len(pixel_shader.byte_code)} bytes, errors={pixel_shader.errors!r}")
	check(pixel_shader.assembly.lstrip().startswith(";") and "Output signature" in pixel_shader.assembly and "SV_Target" in pixel_shader.assembly,
		"MainPS disassembly is returned")
	check(len({r.hash for r in vertex_shaders}) == len(vertex_shaders), "each MainVS variant has its own hash")
	again = compile_shader(dll, code, "MainPS", SHADER_TYPE_PIXEL, include)
	check(again.ok and again.hash == pixel_shader.hash and again.byte_code == pixel_shader.byte_code, "compiling twice gives the same byte code")

	print("2. Broken shaders")
	broken = code.replace("PixelOut psOut;", "PixelOut psOut = ;", 1)
	assert broken != code
	broken_line = broken[:broken.index("PixelOut psOut = ;")].count("\n") + 1
	r = compile_shader(dll, broken, "MainPS", SHADER_TYPE_PIXEL, include)
	check(not r.ok and not r.byte_code and not r.hash and "error" in r.errors and f":{broken_line}:" in r.errors,
		f"syntax error: FALSE with an error at line {broken_line}: {r.errors.splitlines()[0] if r.errors else ''!r}")
	r = compile_shader(dll, code, "NoSuchFunction", SHADER_TYPE_PIXEL, include)
	check(not r.ok and not r.byte_code and "missing entry point" in r.errors, f"missing entry point: {r.errors.strip().splitlines()[-1] if r.errors else ''!r}")
	r = compile_shader(dll, code, "MainPS", SHADER_TYPE_PIXEL, [])
	check(not r.ok and not r.byte_code and "Common.hlsli" in r.errors, f"missing include path: {r.errors.splitlines()[0] if r.errors else ''!r}")
	# 警告はエラーとして扱う(-WX)
	r = compile_shader(dll, "float4 PS() : SV_Target { float2 v = float4(1, 2, 3, 4); return v.xyxy; }", "PS", SHADER_TYPE_PIXEL, [])
	check(not r.ok and "-Werror" in r.errors, f"warnings are errors: {r.errors.splitlines()[0] if r.errors else ''!r}")
	ok = compile_shader(dll, "float4 PS() : SV_Target { return float4(1, 0, 0, 1); }", "PS", SHADER_TYPE_PIXEL, [])
	check(ok.ok and ok.byte_code[:4] == b"DXBC", "a shader without includes compiles after the failures")

	print("3. Invalid arguments")
	r = compile_shader(dll, code, None, SHADER_TYPE_PIXEL, include)
	check(not r.ok and not r.byte_code and not r.errors, "no function name")
	r = compile_shader(dll, code, "MainPS", SHADER_TYPE_COUNT, include)
	check(not r.ok and not r.byte_code and not r.errors, "shader type out of range")
	data = ShaderData()
	data.function_name = b"MainPS"
	check(dll.CompileShader(ctypes.byref(data)) == 0 and not data.byte_code_error_assembly_hash, "no source")
	check(dll.CompileShader(None) == 0, "null shader data")

	print("4. Shader groups")
	keys = ELEMENTS_TYPES
	packed = pack_for_engine(keys, vertex_shaders)
	vs_id = add_shader_group(dll, SHADER_TYPE_VERTEX, len(keys), packed)
	check(vs_id != INVALID_ID, f"AddShaderGroup(vertex, {len(keys)} shaders) = {vs_id:#x}")
	ps_id = add_shader_group(dll, SHADER_TYPE_PIXEL, 1, pack_for_engine([INVALID_ID], [pixel_shader]))
	check(ps_id != INVALID_ID and ps_id != vs_id, f"AddShaderGroup(pixel, 1 shader) = {ps_id:#x}")
	check(add_shader_group(dll, SHADER_TYPE_VERTEX, len(keys), packed[:-1]) == INVALID_ID, "truncated data: invalid id")
	check(add_shader_group(dll, SHADER_TYPE_VERTEX, len(keys), packed + b"\0") == INVALID_ID, "trailing data: invalid id")
	check(add_shader_group(dll, SHADER_TYPE_VERTEX, len(keys) + 1, packed) == INVALID_ID, "wrong count: invalid id")
	check(add_shader_group(dll, SHADER_TYPE_COUNT, len(keys), packed) == INVALID_ID, "shader type out of range: invalid id")
	check(dll.AddShaderGroup(None) == INVALID_ID, "null group data: invalid id")
	dll.RemoveShaderGroup(vs_id)
	dll.RemoveShaderGroup(ps_id)
	dll.RemoveShaderGroup(INVALID_ID)
	again_id = add_shader_group(dll, SHADER_TYPE_PIXEL, 1, pack_for_engine([INVALID_ID], [pixel_shader]))
	check(again_id != INVALID_ID, f"AddShaderGroup after RemoveShaderGroup = {again_id:#x}")
	dll.RemoveShaderGroup(again_id)

	print()
	if failures:
		print(f"FAILED ({len(failures)})")
		return 1
	print("ALL PASSED")
	return 0


if __name__ == "__main__":
	sys.exit(main())
