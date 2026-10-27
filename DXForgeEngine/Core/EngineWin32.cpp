// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [EngineWin32.cpp]
// 作成日 : 2024/12/20
// 作成者 : 田中ミノル
// 概要
//
// 更新履歴
// 2024/12/20 新規作成
// 2026/10/27 初期化が途中で失敗したときの後始末を直し、engine_shutdown()が無効なウィンドウを破棄しないようにした (#107)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// // ====== インクルード部 ======
#if !defined(SHIPPING) && defined(_WIN64)
#include "Content/ContentLoader.h"
#include "Components/Script.h"
#include "Platform/PlatformType.h"
#include "Platform/Platform.h"
#include "Graphics/Renderer.h"
#include <thread>

using namespace dxforge;
namespace
{
	graphics::render_surface game_window{};

	LRESULT win_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		switch (msg)
		{
		case WM_DESTROY:
		{
			if (game_window.window.is_closed())
			{
				PostQuitMessage(0);
				return 0;
			}
		}
		break;

		case WM_SYSCHAR:
			if (wparam == VK_RETURN && (HIWORD(lparam) & KF_ALTDOWN))
			{
				game_window.window.set_fullscrean(!game_window.window.is_fullscreen());
				return 0;
			}
			break;
		}
		return DefWindowProc(hwnd, msg, wparam, lparam);
	}
}	// 匿名名前空間

/// @brief エンジンの初期化
/// @return 初期化に成功した場合はtrueを返す
bool engine_initialize()
{
	// ゲームをロード
	if (!dxforge::content::load_game())
	{
		// コンソールがないので、失敗したことはデバッガの出力に出す
		OutputDebugStringA("[DXForge] failed to load game.bin\n");
		return false;
	}

	// windowの作成
	platform::window_init_info init_info
	{
		&win_proc,nullptr,L"DXForge Game"	// TODO: 読み込んだゲームファイルからゲーム名を取得する。
	};
	game_window.window = platform::create_window(&init_info);
	if (!game_window.window.is_valid())
	{	// ウィンドウの作成に失敗した場合は、読み込んだゲームを破棄してfalseを返す。
		OutputDebugStringA("[DXForge] failed to create the game window\n");
		content::unload_game();
		return false;
	}

	// すべての初期化が完了した場合はtrueを返す。
	return true;
}

/// @brief エンジンの更新
void engine_update()
{
	// 仮の処理
	dxforge::script::update(10.0f);
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

/// @brief エンジンの終了処理
void engine_shutdown()
{
	// ウィンドウの破棄
	// NOTE: engine_initialize()が失敗してもengine_shutdown()は呼ばれるので、作成済みのときだけ破棄する (#107)
	if (game_window.window.is_valid())
	{
		platform::remove_window(game_window.window.get_id());
		game_window.window = {};
	}
	// ゲームのアンロード
	dxforge::content::unload_game();
}

#endif // !defined(SHIPPING)