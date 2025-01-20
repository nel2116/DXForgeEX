// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12LightCulling.cpp]
// 作成日 : 2025/01/19
// 作成者 : 田中ミノル
// 概要 :
//
// 更新履歴
// 2025/01/19 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "D3D12LightCulling.h"
#include "D3D12Core.h"
#include "Shaders/SharedTypes.h"
#include "D3D12Shaders.h"
#include "D3D12Light.h"
#include "D3D12Camera.h"
#include "D3D12GPass.h"

namespace dxforge::graphics::d3d12::delight
{
	namespace
	{

	}	// 匿名名前空間

	bool initialize()
	{
		return true;
	}

	void shutdown()
	{

	}

	void cull_lights(id3d12_graphics_command_list* const cmd_list, const d3d12_frame_info& d3d12_info, d3dx::d3d12_resource_barrier& barriers)
	{

	}
}	// namespace dxforge::graphics::d3d12::delight
