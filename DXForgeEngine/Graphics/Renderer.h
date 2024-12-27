// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Renderer.h]
// 作成日 : 2024/12/20
// 作成者 : 田中ミノル
// 概要
// 　レンダラー
// 更新履歴
// 2024/12/20 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "..\Platform\Window.h"


namespace dxforge::graphics
{
	/// @brief サーフェス
	/// @details グラフィックをメインウィンドウに表示するためのクラス
	class surface
	{
	};

	struct render_surface
	{
		platform::window window{};
		surface surface{};
	};

	enum class graphics_platform
	{
		direct3d12 = 0,
		vulkan,
		opengl,
	};

	bool initialize(graphics_platform platform);
	void shutdown();
}