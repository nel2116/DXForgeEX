// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [OpenGLInterface.cpp]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要
// 　OpenGLのインターフェース
// 更新履歴
// 2024/12/27 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "OpenGLInterface.h"
#include "OpenGLCore.h"
#include "Graphics\GraphicsPlatformInterface.h"

namespace dxforge::graphics::opengl
{
	void get_platform_interface(platform_interface& pi)
	{
		pi.initialize = core::initialize;
		pi.shutdown = core::shutdown;
		pi.render = core::render;
	}

}	// namespace dxforge::graphics
