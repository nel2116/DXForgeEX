// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [InputWin32.h]
// 作成日 : 2025/01/19
// 作成者 : 田中ミノル
// 概要 :
// Win32の入力処理
// 更新履歴
// 2025/01/19 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#ifdef _WIN64
#include "CommonHeaders.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN
#include <Windows.h>

namespace dxforge::input
{
	HRESULT process_input_message(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
}

#endif // _WIN64