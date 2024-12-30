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
#include "Platform/Window.h"

namespace dxforge::graphics
{
	struct platform_interface
	{
		bool(*initialize)(void);
		void(*shutdown)(void);

		struct
		{
			surface(*create)(platform::window);
			void(*remove)(surface_id);
			void(*resize)(surface_id, u32, u32);
			u32(*width)(surface_id);
			u32(*height)(surface_id);
			void(*render)(surface_id);
		} surface;
	};
}