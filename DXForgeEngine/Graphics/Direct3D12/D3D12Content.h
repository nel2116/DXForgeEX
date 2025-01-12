// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Content.h]
// 作成日 : 2025/01/12
// 作成者 : 田中ミノル
// 概要 :
// Direct3D12のコンテンツ
// 更新履歴
// 2025/01/12 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12CommonHeaders.h"

namespace dxforge::graphics::d3d12::content
{
	namespace submesh
	{
		id::id_type add(const u8*& data);
		void remove(id::id_type id);


	}	// namespace submesh

}	// namespace dxforge::graphics::d3d12::content

