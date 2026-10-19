// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [PlatformType.h]
// 作成日 : 2024/12/20
// 作成者 : 田中ミノル
// 概要 :
//  プラットフォームの型
// 更新履歴
// 2024/12/20 新規作成
// // _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"

#ifdef _WIN64
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace dxforge::platform
{
	using window_proc = LRESULT(*)(HWND, UINT, WPARAM, LPARAM);
	using window_handle = HWND;

	struct window_init_info
	{
		window_proc callback{ nullptr };	// ウィンドウプロシージャ
		window_handle parent{ nullptr };	// 親ウィンドウ
		const wchar_t* caption{ nullptr };	// キャプション
		s32 left{ 0 };						// 左上座標
		s32 top{ 0 };						// 左上座標
		u32 width{ 1920 };					// ウィンドウ幅
		u32 height{ 1080 };					// ウィンドウ高さ
	};
}
#endif // !_WIN64


