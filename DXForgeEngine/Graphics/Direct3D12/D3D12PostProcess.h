// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12PostProcess.h]
// 作成日 : 2025/01/08
// 作成者 : 田中ミノル
// 概要 :
// ポストプロセスのヘッダファイル
// 更新履歴
// 2025/01/08 新規作成
// // _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12CommonHeaders.h"

namespace dxforge::graphics::d3d12
{
	struct d3d12_frame_info;
}	// namespace dxforge::graphics::d3d12

namespace dxforge::graphics::d3d12::fx
{
	bool initialize();
	void shutdown();

	void post_process(id3d12_graphics_command_list* cmd_list, const d3d12_frame_info& d3d12_info, D3D12_CPU_DESCRIPTOR_HANDLE target_rtv);
}	// namespace dxforge::graphics::d3d12::fx
