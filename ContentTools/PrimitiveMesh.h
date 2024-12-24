// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [PrimitiveMesh.h]
// 作成日 : 2024/12/24
// 作成者 : 田中ミノル
// 概要 :
//
// 更新履歴
// 2024/12/24 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "ToolsCommon.h"

namespace dxforge::tools
{
	enum primitive_mesh_type : u32
	{
		plane,
		cube,
		uv_sphere,
		ico_sphere,
		cylinder,
		capsule,

		count
	};

	struct primitive_init_info
	{
		primitive_mesh_type type;	// メッシュの種類
		u32 segments[3]{ 1,1,1 };	// 各軸の分割数
		math::v3 size{ 1,1,1 };		// 各軸のサイズ
		u32 lod{ 0 };				// LODのレベル
	};
}