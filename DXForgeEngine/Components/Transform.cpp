// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Transform.cpp]
// 作成日 : 2024/11/11
// 作成者 : 田中ミノル
// 概要
// 　Transformを実装したファイル
// 更新履歴
// 2024/11/11 新規作成
// 2025/01/07 コメント修正
// /_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Transform.h"
#include "Entity.h"

namespace dxforge::transform
{
	namespace
	{
		utl::vector<math::v4> rotations;	// 回転値
		utl::vector<math::v3> positions;	// 位置値
		utl::vector<math::v3> scales;		// スケール値

	}	// 匿名名前空間

	/// @brief Transformコンポーネントの作成
	/// @param info Transformの初期化情報
	/// @param entity コンポーネントを追加するエンティティ
	/// @return component
	component create(init_info info, game_entity::entity entity)
	{
		// エンティティが有効かどうかを確認
		assert(entity.is_valid());
		const id::id_type entity_index{ id::index(entity.get_id()) };

		// 追加しようとしているコンポーネントの長さが
		// エンティティを配置する配列の長さの内にある場合は
		if (positions.size() > entity_index)
		{
			// そのスロットを取得した新しい値で上書きする
			// つまり再利用を行う
			rotations[entity_index] = math::v4(info.rotation);
			positions[entity_index] = math::v3(info.position);
			scales[entity_index] = math::v3(info.scale);
		}
		else
		{
			// そうでない場合は新しい値を追加する
			assert(positions.size() == entity_index);
			rotations.emplace_back(info.rotation);
			positions.emplace_back(info.position);
			scales.emplace_back(info.scale);
		}

		return component{ transform_id{ entity.get_id() } };
	}

	void remove([[maybe_unused]] component c)
	{
		assert(c.is_valid());
	}

	/// @brief Transformコンポーネントの回転値を取得
	/// @return math::v4
	math::v4 component::rotation() const
	{
		assert(is_valid());
		return rotations[id::index(_id)];
	}

	/// @brief Transformコンポーネントの位置値を取得
	/// @return math::v3
	math::v3 component::position() const
	{
		assert(is_valid());
		return positions[id::index(_id)];
	}

	/// @brief Transformコンポーネントのスケール値を取得
	/// @return math::v3
	math::v3 component::scale() const
	{
		assert(is_valid());
		return scales[id::index(_id)];
	}
}
