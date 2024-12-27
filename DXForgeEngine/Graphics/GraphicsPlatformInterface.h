// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [GraphicsPlatformInterface.h]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要
// 　グラフィックスプラットフォームのインターフェース
// 更新履歴
// 2024/12/27 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "Renderer.h"

namespace dxforge::graphics
{
	struct platform_interface
	{
		bool(*initialize)(void);
		void(*shutdown)(void);
	};
}