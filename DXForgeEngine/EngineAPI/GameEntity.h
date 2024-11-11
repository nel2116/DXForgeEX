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
#include "TransformComponent.h"

namespace dxforge::game_entity
{
	DEFINE_TYPED_ID(entity_id);

	/// <summary>
	/// ゲームエンティティを表すクラス
	/// </summary>
	class entity
	{
	public:
		constexpr explicit entity(entity_id id) : _id(id) {}
		constexpr entity() : _id(id::invalid_id) {}
		/// <summary>
		/// ゲームエンティティの ID を取得する
		/// </summary>
		/// <returns>ゲームエンティティのID</returns>
		constexpr entity_id get_id() const { return _id; }
		/// <summary>
		/// ゲームエンティティが有効かどうかを判定する
		/// </summary>
		/// <returns>有効なら true、無効なら false</returns>
		constexpr bool is_valid() const { return id::is_valid(_id); }

		transform::component transform() const;

	private:
		/// <summary>
		/// ゲームエンティティのID
		/// </summary>
		entity_id _id;
	};
}