// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [OpenGLCore.h]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要
// 　OpenGLのコア
// 更新履歴
// 2024/12/27 新規作成
// 2026/10/29 initialize()に初期化情報を渡すようにした (#127)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "Graphics/Renderer.h"

namespace dxforge::graphics::opengl::core
{
	bool initialize(const graphics_init_info& info);
	void shutdown(void);
	void render(void);

}	// namespace dxforge::graphics
