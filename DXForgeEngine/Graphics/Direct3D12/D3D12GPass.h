// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12GPass.h]
// 作成日 : 2025/01/07
// 作成者 : 田中ミノル
// 概要 :
// GPassのヘッダファイル
// 更新履歴
// 2025/01/07 新規作成
// // _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12CommonHeaders.h"

namespace dxforge::graphics::d3d12
{
	struct d3d12_frame_info;
}	// namespace dxforge::graphics::d3d12

namespace dxforge::graphics::d3d12::gpass
{

	constexpr DXGI_FORMAT main_buffer_format{ DXGI_FORMAT_R16G16B16A16_FLOAT };	// メインバッファのフォーマット
	constexpr DXGI_FORMAT depth_buffer_format{ DXGI_FORMAT_D32_FLOAT };			// デプスバッファのフォーマット

	struct opaque_root_parameter
	{
		enum parameter : u32
		{
			global_shader_data,
			position_buffer,
			element_buffer,
			srv_indices,
			per_object_data,

			count
		};
	};

	bool initialize();
	void shutdown();

	[[nodiscard]] const d3d12_render_texture& main_buffer(void);
	[[nodiscard]] const d3d12_depth_buffer& depth_buffer(void);

	// NOTE: gpassで何かをレンダリングする前に、毎フレームこれを呼び出す。
	void set_size(math::u32v2 size);
	void depth_prepass(id3d12_graphics_command_list* cmd_list, const d3d12_frame_info& d3d12_info);
	void render(id3d12_graphics_command_list* cmd_list, const d3d12_frame_info& d3d12_info);

	void add_transitions_for_depth_prepass(d3dx::d3d12_resource_barrier& barriers);
	void add_transitions_for_gpass(d3dx::d3d12_resource_barrier& barriers);
	void add_transitions_for_post_process(d3dx::d3d12_resource_barrier& barriers);

	void set_render_targets_for_depth_prepass(id3d12_graphics_command_list* cmd_list);
	void set_render_targets_for_gpass(id3d12_graphics_command_list* cmd_list);

}	// namespace dxforge::graphics::d3d12::gpass

