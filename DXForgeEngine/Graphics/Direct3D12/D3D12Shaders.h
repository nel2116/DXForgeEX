// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Shaders.h]
// 作成日 : 2025/01/05
// 作成者 : 田中ミノル
// 概要 :
// Direct3D12のシェーダーを扱うクラス
// 更新履歴
// 2025/01/05 新規作成
// // _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12CommonHeaders.h"

namespace dxforge::graphics::d3d12::shaders
{
	struct shader_type
	{
		enum type :u32
		{
			vertex = 0,
			hull,
			domain,
			geometry,
			pixel,
			compute,
			amplification,
			mesh,

			count
		};
	};

	struct engine_shader
	{
		enum id : u32
		{
			fullscreen_triangle_vs = 0,
			fill_color_ps = 1,
			post_process_ps = 2,

			count
		};
	};

	bool initialize();
	void shutdown();

	D3D12_SHADER_BYTECODE get_engine_shader(engine_shader::id id);

} // namespace dxforge::graphics::d3d12::shaders





