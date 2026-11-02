// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [EngineInit.cpp]
// 作成日 : 2026/11/02
// 作成者 : 田中ミノル
// 概要
// 　エディタからエンジン(シェーダーとグラフィックス)を初期化・終了する
// 更新履歴
// 2026/11/02 新規作成(Primal Day 078 の InitializeEngine / ShutdownEngine の実体。#31)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "EngineInit.h"
#include "ShaderCompilation.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <exception>

namespace dxforge::editor
{
	namespace
	{
		bool engine_initialized{ false };

		void log_exception(const wchar_t* stage, const char* what)
		{
			OutputDebugStringW(L"[DXForgeEngineDLL] ");
			OutputDebugStringW(stage);
			OutputDebugStringW(L": ");
			OutputDebugStringA(what ? what : "");
			OutputDebugStringW(L"\n");
		}
	} // 匿名名前空間

	std::filesystem::path engine_shaders_source_folder(const std::filesystem::path& engine_path)
	{
		if (engine_path.empty()) return {};
		return engine_path / L"DXForgeEngine" / L"Graphics" / L"Direct3D12" / L"Shaders";
	}

	engine_init_error::error_code initialize_engine(const graphics::graphics_init_info& info, const std::filesystem::path& engine_path)
	{
		if (engine_initialized) return engine_init_error::succeeded;

		// NOTE: compile_shaders() の中の std::filesystem は、ソースのフォルダがない・shaders という名前のファイルがある、
		//		 などで例外を投げることがある。エディタ(P/Invoke)に C++ の例外を出さないよう、ここで失敗のコードにする。
		try
		{
			if (!compile_shaders(engine_shaders_source_folder(engine_path)))
			{
				OutputDebugStringW(L"[DXForgeEngineDLL] failed to compile the engine shaders\n");
				return engine_init_error::shader_compilation;
			}
		}
		catch (const std::exception& e)
		{
			log_exception(L"exception while compiling the engine shaders", e.what());
			return engine_init_error::shader_compilation;
		}
		catch (...)
		{
			log_exception(L"exception while compiling the engine shaders", "unknown exception");
			return engine_init_error::shader_compilation;
		}

		// NOTE: graphics::initialize() が false のときは、D3D12 の側(failed_init())で後片付けまで済んでいる。
		//		 ここでもう一度 graphics::shutdown() を呼ぶと、解放済みのフェンスを待ってクラッシュする(#128)ので、
		//		 成功したときだけ engine_initialized を立てて、shutdown_engine() で終了させる。
		try
		{
			if (!graphics::initialize(graphics::graphics_platform::direct3d12, info))
			{
				OutputDebugStringW(L"[DXForgeEngineDLL] failed to initialize the graphics (Direct3D 12)\n");
				return engine_init_error::graphics;
			}
		}
		catch (const std::exception& e)
		{
			log_exception(L"exception while initializing the graphics", e.what());
			return engine_init_error::unknown;
		}
		catch (...)
		{
			log_exception(L"exception while initializing the graphics", "unknown exception");
			return engine_init_error::unknown;
		}

		engine_initialized = true;
		return engine_init_error::succeeded;
	}

	void shutdown_engine()
	{
		if (!engine_initialized) return;
		engine_initialized = false;
		graphics::shutdown();
	}

	bool is_engine_initialized()
	{
		return engine_initialized;
	}
}
