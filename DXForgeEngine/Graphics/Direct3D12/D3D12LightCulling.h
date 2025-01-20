// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12LightCulling.h]
// 作成日 : 2025/01/19
// 作成者 : 田中ミノル
// 概要 :
//
// 更新履歴
// 2025/01/19 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12CommonHeaders.h"

namespace dxforge::graphics::d3d12
{
	struct d3d12_frame_info;
}	// namespace dxforge::graphics::d3d12

namespace dxforge::graphics::d3d12::delight
{
	constexpr u32 light_culling_tile_size{ 16 };	// ライトカリングのタイルサイズ

	bool initialize();
	void shutdown();

	void cull_lights(id3d12_graphics_command_list* const cmd_list, const d3d12_frame_info& d3d12_info, d3dx::d3d12_resource_barrier& barriers);

}	// namespace dxforge::graphics::d3d12::delight
