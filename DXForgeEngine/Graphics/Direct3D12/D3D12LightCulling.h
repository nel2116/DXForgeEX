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

	[[nodiscard]] id::id_type add_culler();
	void remove_culler(id::id_type id);

	void cull_lights(id3d12_graphics_command_list* const cmd_list, const d3d12_frame_info& d3d12_info, d3dx::d3d12_resource_barrier& barriers);

	// TODO: ライトのカリングを視覚化するための一時的なもの。 後で取り除く。
	D3D12_GPU_VIRTUAL_ADDRESS frustums(id::id_type id, u32 frame_index);
}	// namespace dxforge::graphics::d3d12::delight
