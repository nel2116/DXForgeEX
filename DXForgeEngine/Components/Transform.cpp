// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Transform.cpp]
// 作成日 : 2024/11/11
// 作成者 : 田中ミノル
// 概要
// 　Transformを実装したファイル
// 更新履歴
// 2024/11/11 新規作成
// /_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Transform.h"
#include "Entity.h"

namespace dxforge::transform
{
	namespace
	{
		utl::vector<math::v4> rotations;
		utl::vector<math::v3> positions;
		utl::vector<math::v3> scales;

	}
	component create_transform(const init_info& info, game_entity::entity entity)
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

		return component(transform_id((id::id_type)positions.size() - 1));
	}
	void remove_transform(component c)
	{
		assert(c.is_valid());
	}
	math::v4 component::rotation() const
	{
		assert(is_valid());
		return rotations[id::index(_id)];
	}
	math::v3 component::position() const
	{
		assert(is_valid());
		return positions[id::index(_id)];
	}
	math::v3 component::scale() const
	{
		assert(is_valid());
		return scales[id::index(_id)];
	}
}
