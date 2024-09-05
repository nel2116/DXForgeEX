// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [GameEntity.h]
// 作成日 : 2024/08/12
// 作成者 : 田中ミノル
// 概要
// 　外部に公開するentityクラスを定義したファイル
// 更新履歴
// 2024/08/12 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "..\Components\ComponentsCommon.h"

namespace dxforge::game_entity
{
	DEFINE_TYPED_ID(entity_id);

	class entity
	{
	public:
		constexpr explicit entity(entity_id id) : _id(id) {}
		constexpr explicit entity() : _id(id::invalid_id) {}

	private:
		entity_id _id;
	};
}