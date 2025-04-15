// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Platform.h]
// 作成日 : 2024/12/20
// 作成者 : 田中ミノル
// 概要 :
// 　プラットフォーム
// 更新履歴
// 2024/12/20 新規作成
// // _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "Window.h"

namespace dxforge::platform
{
	struct window_init_info;

	window create_window(const window_init_info* const init_info = nullptr);
	void remove_window(window_id id);
}
