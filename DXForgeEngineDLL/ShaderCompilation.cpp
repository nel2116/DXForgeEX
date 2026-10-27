// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ShaderCompilation.cpp]
// 作成日 : 2025/01/07
// 作成者 : 田中ミノル
// 概要
// 　シェーダーのコンパイルを行う
// 更新履歴
// 2025/01/07 新規作成
// 2025/01/13 ユーザー定義のシェーダーをコンパイルする処理を追加
// 2026/10/18 引数の個数をUINT32へ明示的にキャスト(C4244の警告対応)
// 2026/10/27 EngineTestからDXForgeEngineDLLへ移動し、エディタからメモリ上のソースをコンパイルできるようにした(Primal Day 076)。
//            DXCは実行時に読み込み、エラーと逆アセンブルも返せるようにした
// 2026/10/27 TILE_SIZEを定義する条件が常に真だったのを修正し、ライトカリングの2つのコンピュートシェーダだけに渡すようにした(#101)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include <fstream>
#include <filesystem>

#include "ShaderCompilation.h"
#include "Graphics\Direct3D12\D3D12Shaders.h"
// NOTE: dxcapi.h と d3d12shader.h はプロジェクトのインクルードパスから探す
//       (EngineTest は packages\DirectXShaderCompiler\inc、DXForgeEngineDLL は NuGet の Microsoft.Direct3D.DXC)。
//       Windows のヘッダーの後にインクルードする。
#include <dxcapi.h>
#include <d3d12shader.h>
#include "Content/ContentToEngine.h"
#include "Utilities/IOStream.h"

using namespace dxforge;
using namespace dxforge::graphics::d3d12::shaders;
using namespace Microsoft::WRL;

// DXCの呼び出しが失敗したら出力に表示する。
// NOTE: D3D12のDXCallと違って止めない。エディタのプロセスで __debugbreak するとエディタが落ちるため。
#define DXC_CALL(x)									\
if (FAILED(x)) {									\
	char line_number[32];							\
	sprintf_s(line_number, "%u", __LINE__);			\
	OutputDebugStringA("Error in: ");				\
	OutputDebugStringA(__FILE__);					\
	OutputDebugStringA("\nLine: ");					\
	OutputDebugStringA(line_number);				\
	OutputDebugStringA("\n");						\
	OutputDebugStringA(#x);							\
	OutputDebugStringA("\n");						\
}

namespace
{
	constexpr const char* shaders_source_path{ "../../DXForgeEngine/Graphics/Direct3D12/Shaders/" };

	struct engine_shader_info
	{
		engine_shader::id   id;
		shader_file_info    info;
	};

	constexpr engine_shader_info engine_shader_files[]
	{
		{engine_shader::fullscreen_triangle_vs, {"FullScreenTriangle.hlsl", "FullScreenTriangleVS", graphics::shader_type::vertex}},
		{engine_shader::fill_color_ps, {"FillColor.hlsl", "FillColorPS", graphics::shader_type::pixel}},
		{engine_shader::post_process_ps, {"PostProcess.hlsl", "PostProcessPS", graphics::shader_type::pixel}},
		{engine_shader::grid_frustums_cs,{"GridFrustums.hlsl","ComputeGridFrustumsCS",graphics::shader_type::compute}},
		{engine_shader::light_culling_cs,{"CullLights.hlsl","CullLightsCS",graphics::shader_type::compute}}
	};

	static_assert(_countof(engine_shader_files) == engine_shader::count);

	struct  dxc_compiled_shader
	{
		ComPtr<IDxcBlob>        byte_code;
		ComPtr<IDxcBlobUtf8>    errors;
		ComPtr<IDxcBlobUtf8>    disassembly;
		DxcShaderHash           hash;
	};

	// DXC(dxcompiler.dll)をこのモジュール(EngineTest.exe か DXForgeEngineDLL.dll)と同じフォルダから読み込み、
	// DxcCreateInstance を返す。読み込めなければ nullptr。
	// NOTE: dxcompiler.lib をリンクすると、dxcompiler.dll が無いだけで DXForgeEngineDLL.dll 自体を読み込めなくなり、
	//       エディタのエンジンの機能がすべて使えなくなる。実行時に読み込めば、失敗するのはシェーダーのコンパイルだけになる。
	DxcCreateInstanceProc get_dxc_create_instance()
	{
		static const DxcCreateInstanceProc create_instance{ []() -> DxcCreateInstanceProc
		{
			HMODULE self{ nullptr };
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&get_dxc_create_instance), &self))
			{
				return nullptr;
			}

			std::wstring module_path(MAX_PATH, L'\0');
			for (;;)
			{
				const DWORD length{ GetModuleFileNameW(self, module_path.data(), (DWORD)module_path.size()) };
				if (length == 0) return nullptr;
				if (length < module_path.size())
				{
					module_path.resize(length);
					break;
				}
				module_path.resize(module_path.size() * 2);
			}

			const std::filesystem::path folder{ std::filesystem::path{ module_path }.parent_path() };
			// dxcompiler.dll は署名のために dxil.dll を名前だけで読み込む。実行ファイルのフォルダ以外
			// (Python の ctypes から DXForgeEngineDLL.dll を読み込んだ場合など)でも見つかるように先に読み込んでおく。
			LoadLibraryExW((folder / L"dxil.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
			HMODULE dxc{ LoadLibraryExW((folder / L"dxcompiler.dll").c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH) };
			if (!dxc)
			{
				OutputDebugStringW(L"dxcompiler.dll could not be loaded.\n");
				return nullptr;
			}

			return reinterpret_cast<DxcCreateInstanceProc>(GetProcAddress(dxc, "DxcCreateInstance"));
		}() };

		return create_instance;
	}

	class shader_compiler
	{
	public:
		shader_compiler()
		{
			const DxcCreateInstanceProc create_instance{ get_dxc_create_instance() };
			if (!create_instance) return;

			HRESULT hr{ S_OK };
			DXC_CALL(hr = create_instance(CLSID_DxcCompiler, IID_PPV_ARGS(&_compiler)));
			if (FAILED(hr)) return;
			DXC_CALL(hr = create_instance(CLSID_DxcUtils, IID_PPV_ARGS(&_utils)));
			if (FAILED(hr)) return;
			DXC_CALL(hr = _utils->CreateDefaultIncludeHandler(&_include_handler));
			if (FAILED(hr)) return;
		}

		DISABLE_COPY_AND_MOVE(shader_compiler);

		bool is_valid() const { return _compiler && _utils && _include_handler; }

		dxc_compiled_shader compile(const u8* data, u32 data_size, graphics::shader_type::type type, const char* function, dxforge::utl::vector<std::wstring>& extra_args)
		{
			assert(data && data_size && function);
			assert(type < graphics::shader_type::count);
			if (!is_valid()) return {};
			HRESULT hr{ S_OK };

			// エディタはソースをUTF-8で渡す。
			ComPtr<IDxcBlobEncoding> source_blob{ nullptr };
			DXC_CALL(hr = _utils->CreateBlob(data, data_size, DXC_CP_UTF8, &source_blob));
			if (FAILED(hr)) return {};
			assert(source_blob && source_blob->GetBufferSize());

			shader_file_info info{};
			info.function = function;
			info.type = type;

			OutputDebugStringA("Compiling ");
			OutputDebugStringA(function);
			OutputDebugStringA("\n");
			return compile(source_blob.Get(), get_args(info, extra_args), DXC_CP_UTF8);
		}

		dxc_compiled_shader compile(shader_file_info info, std::filesystem::path full_path, dxforge::utl::vector<std::wstring>& extra_args)
		{
			if (!is_valid()) return {};
			HRESULT hr{ S_OK };

			// Utilsインターフェイスを使ってソースファイルを読み込む
			ComPtr<IDxcBlobEncoding> source_blob{ nullptr };
			DXC_CALL(hr = _utils->LoadFile(full_path.c_str(), nullptr, &source_blob));
			if (FAILED(hr)) return {};
			assert(source_blob && source_blob->GetBufferSize());

			OutputDebugStringA("Compiling ");
			OutputDebugStringA(info.file_name);
			OutputDebugStringA(" : ");
			OutputDebugStringA(info.function);
			OutputDebugStringA("\n");
			return compile(source_blob.Get(), get_args(info, extra_args), DXC_CP_ACP);
		}

		// コンパイルに失敗したときは、byte_code が空で errors だけを持つ結果を返す。
		dxc_compiled_shader compile(IDxcBlobEncoding* source_blob, dxforge::utl::vector<std::wstring> compiler_args, u32 encoding)
		{
			DxcBuffer buffer{};
			buffer.Encoding = encoding; // DXC_CP_ACP はテキスト形式の自動検出かな？
			buffer.Ptr = source_blob->GetBufferPointer();
			buffer.Size = source_blob->GetBufferSize();

			utl::vector<LPCWSTR> args;
			for (const auto& arg : compiler_args)
			{
				args.emplace_back(arg.c_str());
			}

			HRESULT hr{ S_OK };
			ComPtr<IDxcResult> results{ nullptr };
			DXC_CALL(hr = _compiler->Compile(&buffer, args.data(), (u32)args.size(), _include_handler.Get(), IID_PPV_ARGS(&results)));
			if (FAILED(hr)) return {};
			ComPtr<IDxcBlobUtf8> errors{ nullptr };
			DXC_CALL(hr = results->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr));
			if (FAILED(hr)) return {};
			if (errors && errors->GetStringLength())
			{
				OutputDebugStringA("\nShader compilation error: \n");
				OutputDebugStringA(errors->GetStringPointer());
			}
			else
			{
				OutputDebugStringA(" [ Succeeded ]");
			}
			OutputDebugStringA("\n");
			HRESULT status{ S_OK };
			DXC_CALL(hr = results->GetStatus(&status));
			if (FAILED(hr)) return {};
			if (FAILED(status))
			{
				dxc_compiled_shader result{};
				result.errors = errors;
				return result;
			}

			ComPtr<IDxcBlob> hash{ nullptr };
			DXC_CALL(hr = results->GetOutput(DXC_OUT_SHADER_HASH, IID_PPV_ARGS(&hash), nullptr));
			if (FAILED(hr)) return {};
			DxcShaderHash* const hash_buffer{ (DxcShaderHash* const)hash->GetBufferPointer() };
			// ソースコードが異なれば、同じバイトコードになる可能性がある。
			assert(!(hash_buffer->Flags & DXC_HASHFLAG_INCLUDES_SOURCE));
			OutputDebugStringA("Shader hash: ");
			for (u32 i{ 0 }; i < _countof(hash_buffer->HashDigest); ++i)
			{
				char hash_bytes[3]{};	// 2文字の16進数文字列とヌル文字
				sprintf_s(hash_bytes, "%02X", hash_buffer->HashDigest[i]);
				OutputDebugStringA(hash_bytes);
				OutputDebugStringA(" ");
			}
			OutputDebugStringA("\n");

			ComPtr<IDxcBlob> shader{ nullptr };
			DXC_CALL(hr = results->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader), nullptr));
			if (FAILED(hr)) return {};
			buffer.Ptr = shader->GetBufferPointer();
			buffer.Size = shader->GetBufferSize();

			ComPtr<IDxcResult> disasm_results{ nullptr };
			ComPtr<IDxcBlobUtf8> disassembly{ nullptr };
			DXC_CALL(hr = _compiler->Disassemble(&buffer, IID_PPV_ARGS(&disasm_results)));
			if (SUCCEEDED(hr))
			{
				DXC_CALL(hr = disasm_results->GetOutput(DXC_OUT_DISASSEMBLY, IID_PPV_ARGS(&disassembly), nullptr));
			}

			dxc_compiled_shader result{ shader.Detach(), errors.Detach(), disassembly.Detach() };
			memcpy(&result.hash.HashDigest[0], &hash_buffer->HashDigest[0], _countof(hash_buffer->HashDigest));

			return result;
		}

	private:	// プライベート関数
		utl::vector<std::wstring> get_args(const shader_file_info& info, utl::vector<std::wstring>& extra_args)
		{
			utl::vector<std::wstring> args{};

			// エラーの表示に使うソースファイル名(メモリ上のソースには無い)
			if (info.file_name) args.emplace_back(to_wstring(info.file_name));
			args.emplace_back(L"-E");
			args.emplace_back(to_wstring(info.function));
			args.emplace_back(L"-T");
			args.emplace_back(to_wstring(_profile_string[(u32)info.type]));
			args.emplace_back(L"-I");
			args.emplace_back(to_wstring(shaders_source_path));
			args.emplace_back(L"-enable-16bit-types");
			args.emplace_back(DXC_ARG_ALL_RESOURCES_BOUND);
#if _DEBUG
			args.emplace_back(DXC_ARG_DEBUG);
			args.emplace_back(DXC_ARG_SKIP_OPTIMIZATIONS);
#else
			args.emplace_back(DXC_ARG_OPTIMIZATION_LEVEL3);
#endif
			args.emplace_back(DXC_ARG_WARNINGS_ARE_ERRORS);
			args.emplace_back(L"-Qstrip_reflect");
			args.emplace_back(L"-Qstrip_debug");

			for (const auto& arg : extra_args)
			{
				args.emplace_back(arg.c_str());
			}

			return args;
		}



	private:	// メンバ変数
		// NOTE: シェーダーモデル6.xも使用できます（ASとMSはSM6.5以降のみサポート）。
		constexpr static const char* _profile_string[]{ "vs_6_6","hs_6_6", "ds_6_6", "gs_6_6", "ps_6_6", "cs_6_6", "as_6_6", "ms_6_6" };
		static_assert(_countof(_profile_string) == graphics::shader_type::count);

		ComPtr<IDxcCompiler3> _compiler{ nullptr };
		ComPtr<IDxcUtils> _utils{ nullptr };
		ComPtr<IDxcIncludeHandler> _include_handler{ nullptr };
	};

	// コンパイルされたシェーダのバイナリファイルへのパスを取得する。
	decltype(auto) get_engine_shaders_path()
	{
		return std::filesystem::path{ graphics::get_engine_shaders_path(graphics::graphics_platform::direct3d12) };
	}

	bool compiled_shaders_are_up_to_date()
	{
		auto engine_shaders_path = get_engine_shaders_path();
		if (!std::filesystem::exists(engine_shaders_path)) return false;
		auto shaders_compilation_time = std::filesystem::last_write_time(engine_shaders_path);

		for (const auto& entry : std::filesystem::directory_iterator{ shaders_source_path })
		{
			if (entry.last_write_time() > shaders_compilation_time)
			{
				return false;
			}
		}

		return true;
	}

	bool save_compiled_shaders(utl::vector<dxc_compiled_shader>& shaders)
	{
		auto engine_shaders_path = get_engine_shaders_path();
		std::filesystem::create_directories(engine_shaders_path.parent_path());
		std::ofstream file(engine_shaders_path, std::ios::out | std::ios::binary);
		if (!file || !std::filesystem::exists(engine_shaders_path))
		{
			file.close();
			return false;
		}

		for (const auto& shader : shaders)
		{
			const D3D12_SHADER_BYTECODE byte_code{ shader.byte_code->GetBufferPointer(), shader.byte_code->GetBufferSize() };
			file.write(reinterpret_cast<const char*>(&byte_code.BytecodeLength), sizeof(byte_code.BytecodeLength));
			file.write(reinterpret_cast<const char*>(&shader.hash.HashDigest[0]), _countof(shader.hash.HashDigest));
			file.write(reinterpret_cast<const char*>(byte_code.pShaderBytecode), byte_code.BytecodeLength);
		}

		file.close();
		return true;
	}

	u64 string_length(const ComPtr<IDxcBlobUtf8>& blob)
	{
		return blob ? blob->GetStringLength() : 0;
	}

	std::unique_ptr<u8[]> pack_compiled_shader(const dxc_compiled_shader& compiled_shader, bool include_errors_and_disassembly)
	{
		const bool has_byte_code{ compiled_shader.byte_code && compiled_shader.byte_code->GetBufferPointer() && compiled_shader.byte_code->GetBufferSize() };
		// エラーと逆アセンブルを返さないときは、バイトコードが無ければ返すものが無い。
		if (!has_byte_code && !(include_errors_and_disassembly && string_length(compiled_shader.errors))) return {};

		static_assert(content::compiled_shader::hash_length == _countof(DxcShaderHash::HashDigest));
		const u64 byte_code_size{ has_byte_code ? compiled_shader.byte_code->GetBufferSize() : 0 };
		const u64 errors_size{ string_length(compiled_shader.errors) };
		const u64 disassembly_size{ string_length(compiled_shader.disassembly) };
		const u64 extra_size{ include_errors_and_disassembly ? sizeof(u64) + sizeof(u64) + errors_size + disassembly_size : 0 };
		const u64 buffer_size{ sizeof(u64) + content::compiled_shader::hash_length + byte_code_size + extra_size };
		std::unique_ptr<u8[]> buffer{ std::make_unique<u8[]>(buffer_size) };
		utl::blob_stream_writer blob{ buffer.get(), buffer_size };
		blob.write(byte_code_size);
		// コンパイルに失敗したときのハッシュは0のまま
		if (has_byte_code)
		{
			blob.write(compiled_shader.hash.HashDigest, content::compiled_shader::hash_length);
			blob.write((u8*)compiled_shader.byte_code->GetBufferPointer(), byte_code_size);
		}
		else
		{
			blob.skip(content::compiled_shader::hash_length);
		}

		if (include_errors_and_disassembly)
		{
			blob.write(errors_size);
			blob.write(disassembly_size);
			if (errors_size) blob.write((const u8*)compiled_shader.errors->GetStringPointer(), errors_size);
			if (disassembly_size) blob.write((const u8*)compiled_shader.disassembly->GetStringPointer(), disassembly_size);
		}

		assert(blob.offset() == buffer_size);
		return buffer;
	}

}	// 匿名名前空間

std::wstring to_wstring(const char* c)
{
	if (!c || !*c) return {};
	const s32 length{ MultiByteToWideChar(CP_UTF8, 0, c, -1, nullptr, 0) };
	if (length <= 0) return {};
	std::wstring s(length, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, c, -1, s.data(), length);
	s.resize(length - 1); // 終端のヌル文字を除く
	return s;
}

std::unique_ptr<u8[]> compile_shader(shader_file_info info, const u8* code, u32 code_size, utl::vector<std::wstring>& extra_args, bool include_errors_and_disassembly/*=false*/)
{
	assert(!info.file_name && info.function && code && code_size);

	// NOTE: according to marcelolr()
	//		"...コンパイラー・インスタンスを作るのはかなり安いので、キャッシュしたり共有したりする手間をかける価値はないだろう。"
	shader_compiler compiler{};
	return pack_compiled_shader(compiler.compile(code, code_size, info.type, info.function, extra_args), include_errors_and_disassembly);
}

std::unique_ptr<u8[]> compile_shader(shader_file_info info, const char* file_path, utl::vector<std::wstring>& extra_args, bool include_errors_and_disassembly/*=false*/)
{
	std::filesystem::path full_path{ file_path };
	full_path += info.file_name;
	if (!std::filesystem::exists(full_path)) return {};

	// NOTE: according to marcelolr()
	//		"...コンパイラー・インスタンスを作るのはかなり安いので、キャッシュしたり共有したりする手間をかける価値はないだろう。"
	shader_compiler compiler{};
	return pack_compiled_shader(compiler.compile(info, full_path, extra_args), include_errors_and_disassembly);
}


bool compile_shaders()
{
	if (compiled_shaders_are_up_to_date()) return true;

	shader_compiler compiler{};
	utl::vector<dxc_compiled_shader> shaders{};
	std::filesystem::path full_path{};

	// コンパイルされたシェーダーは、コンパイル順と同じ順番でバッファにまとめられる。
	for (u32 i{ 0 }; i < engine_shader::count; ++i)
	{
		auto& file = engine_shader_files[i];

		full_path = shaders_source_path;
		full_path += file.info.file_name;
		if (!std::filesystem::exists(full_path)) return false;
		utl::vector<std::wstring> extra_args{};

		if (file.id == engine_shader::grid_frustums_cs || file.id == engine_shader::light_culling_cs)
		{
			// TODO: d3d12からTILE_SIZE値を取得する
			extra_args.emplace_back(L"-D");
			extra_args.emplace_back(L"TILE_SIZE=16");
		}

		dxc_compiled_shader compiled_shader{ compiler.compile(file.info,full_path,extra_args) };
		if (compiled_shader.byte_code && compiled_shader.byte_code->GetBufferPointer() && compiled_shader.byte_code->GetBufferSize())
		{
			shaders.emplace_back(std::move(compiled_shader));
		}
		else
		{
			return false;
		}
	}

	return save_compiled_shaders(shaders);
}
