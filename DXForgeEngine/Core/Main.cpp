// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Main.cpp]
// 作成日 : 2024/12/2
// 作成者 : 田中ミノル
// 概要
// 　エンジンの提供するエントリーポイント
// 更新履歴
// 2024/12/2 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// // ====== インクルード部 ======


#ifdef  _WIN64
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <crtdbg.h>
#ifndef USE_WITH_EDITOR

extern bool engine_initialize();
extern void engine_update();
extern void engine_shutdown();

// エントリーポイント
int WINAPI WinMain(HINSTANCE, HINSTANCE, LPSTR, int)
{
	// デバッグ時にメモリリーク検出
#if _DEBUG
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif // _DEBUG

	// エンジンの初期化
	if (engine_initialize())
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
		}
	}
	// エンジンの終了処理
	engine_shutdown();
	return 0;
}

#endif // !USE_WITH_EDITOR
#endif //  _WIN64
