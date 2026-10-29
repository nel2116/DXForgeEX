// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [OpenGLCore.cpp]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要
// 　OpenGLのコア
// 更新履歴
// 2024/12/27 新規作成
// 2026/10/29 initialize()に初期化情報を渡すようにした (#127)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "OpenGLCore.h"

namespace dxforge::graphics::opengl::core
{
	bool initialize(const graphics_init_info&)
	{
		return false;
	}
	void shutdown(void)
	{
	}
	void render(void)
	{
	}
}	// namespace dxforge::graphics

