// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Main.cpp]
// 作成日 : 2024/11/11
// 作成者 : 田中ミノル
// 概要 :
// エンジンをテストするためのプログラム
// 更新履歴
// 2024/11/11 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Test.h"
#pragma comment(lib, "DXForgeEngine.lib")

#if TEST_ENTITY_COMPONENTS
#include "TestEntityComponent.h"
#elif TEST_WINDOW
#include "TestWindow.h"
#elif TEST_DLL
#include "TestDll.h"
#elif TEST_RENDERER
#include "TestRenderer.h"
#else
#error いずれかのテストを有効にする必要があります
#endif	// TEST_ENTITY_COMPONENTS


#ifdef _WIN64
#include <Windows.h>
#include <filesystem>

// TODO: IOユーティリティ・ヘッダ/ライブラリを用意して、この関数をそこに移動させた方がいいかもしれない。
std::filesystem::path set_current_directory_to_executable_path()
{
	// 作業ディレクトリを実行パスに設定する
	wchar_t path[MAX_PATH]{};
	const uint32_t length{ GetModuleFileName(0, &path[0], MAX_PATH) };
	if (!length || GetLastError() == ERROR_INSUFFICIENT_BUFFER) return {};
	std::filesystem::path p{ path };
	std::filesystem::current_path(p.parent_path());
	return std::filesystem::current_path();
}

// エントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	// デバッグ時にメモリリーク検出
#if _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif // _DEBUG

	// 作業ディレクトリを実行パスに設定
	set_current_directory_to_executable_path();

	// テストの初期化
	engine_test test{};

	// 初期化
	if (test.initialize())
	{	// 初期化に成功した場合、メインループに入る
		MSG msg{};
		bool is_running{ true };
		// メインループ
		while (is_running)
		{
			// Windowsのメッセージを処理
			while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
				is_running &= (msg.message != WM_QUIT);	// 終了メッセージが来たらループを抜ける
			}

			// テストの実行
			test.run();
		}
	}

	// 終了処理
	test.shutdown();
	return 0;
}


#else
int main()
{
#if _DEBUG
	// メモリリークチェック
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif // _DEBUG

	engine_test test{};

	if (test.initialize())
	{
		test.run();
	}

	test.shutdown();

	return 0;
}
#endif // _WIN64