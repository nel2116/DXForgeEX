// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_EngineInit.cpp]
// 作成日 : 2026/11/02
// 作成者 : 田中ミノル
// 概要 :
// DXForgeEngineDLL/EngineInit.cpp(DLLの InitializeEngine / ShutdownEngine)のうち、GPUの要らない部分のテスト。
// 初期化に成功する場合と、グラフィックスの初期化に失敗する場合は、--render のヘッドレス実行
// (HeadlessRender.cpp、EngineUnitTests/Tests/render_run_test.py)で確かめる。
// 更新履歴
// 2026/11/02 新規作成 (Primal Day 078, #31)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"
#include "../DXForgeEngineDLL/EngineInit.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <string>

using namespace dxforge;
using engine_init_error = editor::engine_init_error;

namespace
{
	/// @brief 一時フォルダの下に <temp>\dxforge_engine_init_<pid>_<name>\a\b を作り、テストの間だけ作業フォルダにする
	/// @details エンジンのシェーダーのソースは ..\..\DXForgeEngine\...(ここにはない)、shaders.bin は .\shaders\d3d12\ を見る。
	class scoped_work_folder
	{
	public:
		explicit scoped_work_folder(const wchar_t* name)
		{
			std::error_code error{};
			_previous = std::filesystem::current_path(error);
			_root = std::filesystem::temp_directory_path(error) /
				(L"dxforge_engine_init_" + std::to_wstring(GetCurrentProcessId()) + L"_" + name);
			std::filesystem::remove_all(_root, error);
			const std::filesystem::path folder{ _root / L"a" / L"b" };
			_valid = std::filesystem::create_directories(folder, error) && SetCurrentDirectoryW(folder.c_str());
		}

		~scoped_work_folder()
		{
			SetCurrentDirectoryW(_previous.c_str());
			std::error_code error{};
			std::filesystem::remove_all(_root, error);
		}

		DISABLE_COPY_AND_MOVE(scoped_work_folder);

		bool is_valid() const { return _valid; }

	private:
		std::filesystem::path _previous{};
		std::filesystem::path _root{};
		bool _valid{ false };
	};

	void check_not_initialized()
	{
		CHECK(!editor::is_engine_initialized());
		// 初期化していないときの shutdown_engine() は何もしない(2回呼んでも)
		editor::shutdown_engine();
		editor::shutdown_engine();
		CHECK(!editor::is_engine_initialized());
	}
} // 匿名名前空間

// 初期化する前に ShutdownEngine を呼んでも、2回呼んでも落ちない
DXFORGE_TEST_CASE(EngineInit_ShutdownWithoutInitializeDoesNothing)
{
	check_not_initialized();
}

// シェーダーのソースが見つからなければ、グラフィックスを初期化せずに shader_compilation を返す
DXFORGE_TEST_CASE(EngineInit_MissingShaderSourcesReturnsShaderCompilation)
{
	scoped_work_folder folder{ L"missing_sources" };
	REQUIRE(folder.is_valid());

	CHECK(editor::initialize_engine() == engine_init_error::shader_compilation);
	check_not_initialized();
	// shaders.bin は作られない
	CHECK(!std::filesystem::exists(L"shaders\\d3d12\\shaders.bin"));
}

// compile_shaders() の中の std::filesystem が例外を投げても(shaders.bin はあるが、ソースのフォルダがない)、
// 例外を外に出さずに shader_compilation を返す
DXFORGE_TEST_CASE(EngineInit_ShaderCompilationExceptionReturnsShaderCompilation)
{
	scoped_work_folder folder{ L"compilation_throws" };
	REQUIRE(folder.is_valid());
	std::error_code error{};
	REQUIRE(std::filesystem::create_directories(L"shaders\\d3d12", error));
	{
		std::ofstream file{ L"shaders\\d3d12\\shaders.bin", std::ios::binary };
		REQUIRE(file.good());
	}

	CHECK(editor::initialize_engine() == engine_init_error::shader_compilation);
	check_not_initialized();
}

// エディタの EngineAPIStructs.EngineInitError と同じ値(DXForgeEditor/Tests/engine_init_test.ps1 も確かめる)
DXFORGE_TEST_CASE(EngineInit_ErrorCodesMatchPrimal)
{
	CHECK(engine_init_error::succeeded == 0);
	CHECK(engine_init_error::unknown == 1);
	CHECK(engine_init_error::shader_compilation == 2);
	CHECK(engine_init_error::graphics == 3);
	CHECK(sizeof(engine_init_error::error_code) == sizeof(u32));
}
