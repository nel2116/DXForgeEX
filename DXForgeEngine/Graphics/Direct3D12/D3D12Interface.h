// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Interface.h]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要
// 　Direct3D12のインターフェース
// 更新履歴
// 2024/12/27 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======

namespace dxforge::graphics
{
	struct platform_interface;

	namespace d3d12
	{
		void get_platform_interface(platform_interface& interface);


	}	// namespace d3d12

}	// namespace dxforge::graphics
