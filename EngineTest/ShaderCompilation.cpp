// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ShaderCompilation.cpp]
// 作成日 : 2025/01/07
// 作成者 : 田中ミノル
// 概要
// 　シェーダーのコンパイルを行う
// 更新履歴
// 2025/01/07 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "ShaderCompilation.h"

#include "..\packages\DirectXShaderCompiler\inc\d3d12shader.h"
#include "..\packages\DirectXShaderCompiler\inc\dxcapi.h"

#include "Graphics/Direct3D12/D3D12Core.h"
#include "Graphics/Direct3D12/D3D12Shaders.h"

#include <fstream>
#include <filesystem>

// NOTE: DXCにNuGetパッケージがあれば、こんなことは必要ない。
#pragma comment(lib, "../packages/DirectXShaderCompiler/lib/x64/dxcompiler.lib")

using namespace dxforge;
using namespace dxforge::graphics::d3d12::shaders;
using namespace Microsoft::WRL;

namespace
{
	struct shader_file_info
	{
		const char* file;
		const char* function;
		engine_shader::id id;
		shader_type::type type;
	};

	constexpr shader_file_info shader_files[]
	{
		{"FullScreenTriangle.hlsl","FullScreenTriangleVS",engine_shader::fullscreen_triangle_vs,shader_type::vertex },
		{"FillColor.hlsl","FillColorPS",engine_shader::fill_color_ps,shader_type::pixel },
	};

	static_assert(_countof(shader_files) == engine_shader::count);

	constexpr const char* shaders_source_path{ "../../DXForgeEngine/Graphics/Direct3D12/Sbaders/" };

	std::wstring to_wstring(const  char* c)
	{
		std::string s{ c };
		return  { s.begin(),s.end() };
	}

	class shader_compiler
	{
	public:
		shader_compiler()
		{
			HRESULT hr{ S_OK };
			DXCall(hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&_compiler)));
			if (FAILED(hr)) return;
			DXCall(hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&_utils)));
			if (FAILED(hr)) return;
			DXCall(hr = _utils->CreateDefaultIncludeHandler(&_include_handler));
			if (FAILED(hr)) return;
		}

		DISABLE_COPY_AND_MOVE(shader_compiler);

		IDxcBlob* compile(shader_file_info info, std::filesystem::path full_path)
		{
			assert(_compiler && _utils && _include_handler);
			HRESULT hr{ S_OK };

			// Utilsインターフェイスを使ってソースファイルを読み込む
			ComPtr<IDxcBlobEncoding> source_blob{ nullptr };
			DXCall(hr = _utils->LoadFile(full_path.c_str(), nullptr, &source_blob));
			if (FAILED(hr)) return nullptr;
			assert(source_blob && source_blob->GetBufferSize());

			std::wstring file{ to_wstring(info.file) };
			std::wstring func{ to_wstring(info.function) };
			std::wstring prof{ to_wstring(_profile_string[(u32)info.type]) };

			LPCWSTR args[]
			{
				file.c_str(),                       // エラー報告用のオプションのシェーダー・ソース・ファイル名
				L"-E", func.c_str(),                // エントリー機能
				L"-T", prof.c_str(),                // ターゲット・プロフィール
				DXC_ARG_ALL_RESOURCES_BOUND,
	#if _DEBUG
				DXC_ARG_DEBUG,
				DXC_ARG_SKIP_OPTIMIZATIONS,
	#else
				DXC_ARG_OPTIMIZATION_LEVEL3,
	#endif
				DXC_ARG_WARNINGS_ARE_ERRORS,
				L"-Qstrip_reflect",                 // 反射を別の塊に分離する
				L"-Qstrip_debug",                   // デバッグ情報を別のブロブに取り出す
			};
			OutputDebugStringA("Compiling ");
			OutputDebugStringA(info.file);
			return compile(source_blob.Get(), args, _countof(args));
		}

		IDxcBlob* compile(IDxcBlobEncoding* source_blob, LPCWSTR* args, u32 num_args)
		{
			DxcBuffer buffer{};
			buffer.Encoding = DXC_CP_ACP; // テキスト形式の自動検出かな？
			buffer.Ptr = source_blob->GetBufferPointer();
			buffer.Size = source_blob->GetBufferSize();
			HRESULT hr{ S_OK };
			ComPtr<IDxcResult> results{ nullptr };
			DXCall(hr = _compiler->Compile(&buffer, args, num_args, _include_handler.Get(), IID_PPV_ARGS(&results)));
			if (FAILED(hr)) return nullptr;
			ComPtr<IDxcBlobUtf8> errors{ nullptr };
			DXCall(hr = results->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr));
			if (FAILED(hr)) return nullptr;
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
			DXCall(hr = results->GetStatus(&status));
			if (FAILED(hr) || FAILED(status)) return nullptr;
			ComPtr<IDxcBlob> shader{ nullptr };
			DXCall(hr = results->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shader), nullptr));
			if (FAILED(hr)) return nullptr;
			return shader.Detach();
		}

	private:
		const char* _profile_string[shader_type::count]{ "vs_6_5","hs_6_5", "ds_6_5", "gs_6_5", "ps_6_5", "cs_6_5", "as_6_5", "ms_6_5" };

		ComPtr<IDxcCompiler3> _compiler{ nullptr };
		ComPtr<IDxcUtils> _utils{ nullptr };
		ComPtr<IDxcIncludeHandler> _include_handler{ nullptr };
	};

	// コンパイルされたシェーダのバイナリファイルへのパスを取得する。
	decltype(auto) get_engine_shaders_path()
	{
		return std::filesystem::absolute(graphics::get_engine_shaders_path(graphics::graphics_platform::direct3d12));
	}

	bool compiled_shaders_are_up_to_date()
	{
		auto engine_shaders_path = get_engine_shaders_path();
		if (!std::filesystem::exists(engine_shaders_path)) return false;
		auto shaders_compilation_time = std::filesystem::last_write_time(engine_shaders_path);

		std::filesystem::path path{};
		std::filesystem::path full_path{};

		// エンジンシェーダーソースファイルのどちらかが、コピーされたシェーダーファイルより新しいかどうかをチェックします。
		// その場合、再コンパイルする必要があります。
		for (u32 i{ 0 }; i < engine_shader::count; ++i)
		{
			auto& info = shader_files[i];

			path = shaders_source_path;
			path += info.file;
			full_path = std::filesystem::absolute(path);
			if (!std::filesystem::exists(full_path)) return false;

			auto shader_file_time = std::filesystem::last_write_time(full_path);
			if (shader_file_time > shaders_compilation_time) return false;
		}

		return true;
	}

	bool save_compiled_shaders(utl::vector<ComPtr<IDxcBlob>>& shaders)
	{
		auto engine_shaders_path = get_engine_shaders_path();
		std::filesystem::create_directories(engine_shaders_path.parent_path());
		std::ofstream file(engine_shaders_path, std::ios::out | std::ios::binary);
		if (!file || !std::filesystem::exists(engine_shaders_path))
		{
			file.close();
			return false;
		}

		for (auto& shader : shaders)
		{
			const D3D12_SHADER_BYTECODE byte_code{ shader->GetBufferPointer(), shader->GetBufferSize() };
			file.write(reinterpret_cast<const char*>(&byte_code.BytecodeLength), sizeof(byte_code.BytecodeLength));
			file.write(reinterpret_cast<const char*>(byte_code.pShaderBytecode), byte_code.BytecodeLength);
		}

		file.close();
		return true;
	}

}	// 匿名名前空間

bool compile_shaders()
{
	if (compiled_shaders_are_up_to_date()) return true;
	utl::vector<ComPtr<IDxcBlob>> shaders{};
	std::filesystem::path path{};
	std::filesystem::path full_path{};

	shader_compiler compiler{};

	// コンパイルされたシェーダーは、コンパイル順と同じ順番でバッファにまとめられる。
	for (u32 i{ 0 }; i < engine_shader::count; ++i)
	{
		auto& info = shader_files[i];

		path = shaders_source_path;
		path += info.file;
		full_path = std::filesystem::absolute(path);
		if (!std::filesystem::exists(full_path)) return false;
		ComPtr<IDxcBlob> compiled_shader{ compiler.compile(info,full_path) };
		if (compiled_shader->GetBufferPointer() && compiled_shader->GetBufferSize() && compiled_shader != nullptr)
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
