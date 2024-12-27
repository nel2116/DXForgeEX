// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Interface.cpp]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要
// 　Direct3D12のインターフェース
// 更新履歴
// 2024/12/27 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "D3D12Interface.h"
#include "D3D12Core.h"
#include "Graphics\GraphicsPlatformInterface.h"

namespace dxforge::graphics::d3d12
{
	void get_platform_interface(platform_interface& pi)
	{
		pi.initialize = core::initialize;
		pi.shutdown = core::shutdown;
	}

}	// namespace dxforge::graphics

