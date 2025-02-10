// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [GeometryComponent.h]
// 作成日 : 2025/02/10
// 作成者 : 田中ミノル
// 概要 :
// 　ジオメトリコンポーネント
// 更新履歴
// 2025/02/10 新規作成
// // _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "../Components/ComponentsCommon.h"

namespace dxforge::geometry
{
	DEFINE_TYPED_ID(geometry_id);

	class component final
	{
	public:
		constexpr explicit component(geometry_id id) : _id{ id } {}
		constexpr component() : _id{ id::invalid_id } {}
		constexpr geometry_id get_id() const { return _id; }
		constexpr bool is_valid() const { return id::is_valid(_id); }

	private:
		geometry_id _id;
	};
}

