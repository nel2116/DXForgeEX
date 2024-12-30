// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [VulkanCore.h]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要
// 　Vulkanのコア
// 更新履歴
// 2024/12/27 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======

namespace dxforge::graphics::vulkan::core
{
	bool initialize(void);
	void shutdown(void);
	void render(void);

}	// namespace dxforge::graphics
