// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [TestRenderer.cpp]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要 :
// 　レンダラのテスト
// 更新履歴
// 2024/12/27 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "..\Platform\PlatformType.h"
#include "..\Platform\Platform.h"
#include "..\Graphics\Renderer.h"
#include "TestRenderer.h"

using namespace dxforge;

graphics::render_surface _surface[4];

LRESULT win_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
{
	switch (msg)
	{
	case WM_DESTROY:
	{
		bool all_closed{ true };
		for (u32 i{ 0 }; i < _countof(_surface); ++i)
		{
			if (!_surface[i].window.is_closed())
			{
				all_closed = false;
			}
		}
		if (all_closed)
		{
			PostQuitMessage(0);
			return 0;
		}
	}
	break;

	case WM_SYSCHAR:
		if (wparam == VK_RETURN && (HIWORD(lparam) & KF_ALTDOWN))
		{
			platform::window win{ platform::window_id{(id::id_type)GetWindowLongPtr(hwnd, GWLP_USERDATA)} };
			win.set_fullscrean(!win.is_fullscreen());
			return 0;
		}
		break;
	}
	return DefWindowProc(hwnd, msg, wparam, lparam);
}

void create_renderer_surface(graphics::render_surface& surface, platform::window_init_info info)
{
	surface.window = platform::create_window(&info);
}

void destroy_renderer_surface(graphics::render_surface& surface)
{
	platform::remove_window(surface.window.get_id());
}

bool engine_test::initialize()
{
	bool result{ graphics::initialize(graphics::graphics_platform::direct3d12) };
	if (!result) return result;

	platform::window_init_info info[]
	{
		{&win_proc,nullptr,L"Test window 1",100,100,400,800},
		{&win_proc,nullptr,L"Test window 2",150,150,800,400},
		{&win_proc,nullptr,L"Test window 3",200,200,400,800},
		{&win_proc,nullptr,L"Test window 4",250,250,800,600},
	};
	static_assert(_countof(info) == _countof(_surface));

	for (u32 i{ 0 }; i < _countof(_surface); ++i)
	{
		create_renderer_surface(_surface[i], info[i]);
	}
	return result;
}

void engine_test::run()
{
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void engine_test::shutdown()
{
	for (u32 i{ 0 }; i < _countof(_surface); ++i)
		destroy_renderer_surface(_surface[i]);
	graphics::shutdown();
}