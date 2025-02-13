// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Transform.cpp]
// 作成日 : 2024/11/11
// 作成者 : 田中ミノル
// 概要
// 　Transformを実装したファイル
// 更新履歴
// 2024/11/11 新規作成
// 2025/01/07 コメントの修正
// 2525/01/12 Orientationの計算を追加
// 2025/01/14 コメントの追加
// /_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Transform.h"
#include "Entity.h"

namespace dxforge::transform
{
	// 匿名名前空間に各種ベクターを定義
	namespace
	{
		utl::vector<math::m4x4> to_world;				///< ワールド行列を格納するベクター
		utl::vector<math::m4x4> inv_world;				///< ワールド行列の逆行列を格納するベクター
		utl::vector<math::v4> rotations;				///< 各エンティティの回転情報を格納するベクター
		utl::vector<math::v3> orientations;				///< 各エンティティの向きを格納するベクター
		utl::vector<math::v3> positions;				///< 各エンティティの位置を格納するベクター
		utl::vector<math::v3> scales;					///< 各エンティティのスケール情報を格納するベクター
		utl::vector<u8> has_transform;					///< 計算済みかどうかを示すフラグ (0: 未計算, 1: 計算済み)
		utl::vector<u8> changes_from_previous_frame;	///< 前回のフレームからの変更を示すフラグ (0: 変更なし, 1: 変更あり)
		u8 read_write_flags;							///< 読み書きフラグ

		/// @brief 指定されたインデックスのTransform行列を計算
		/// @param index 計算対象のエンティティインデックス
		void calculate_transform_matrices(id::id_type index)
		{
			assert(rotations.size() >= index);
			assert(positions.size() >= index);
			assert(scales.size() >= index);

			using namespace DirectX;
			// 回転、位置、スケールのDirectXベクターをロード
			XMVECTOR r{ XMLoadFloat4(&rotations[index]) };
			XMVECTOR t{ XMLoadFloat3(&positions[index]) };
			XMVECTOR s{ XMLoadFloat3(&scales[index]) };

			// アフィン変換行列を生成
			XMMATRIX world{ XMMatrixAffineTransformation(s, XMQuaternionIdentity(), r, t) };
			XMStoreFloat4x4(&to_world[index], world);

			// NOTE: (F. Luna) Intro to DirectX 12, section 8.2.2
			// 逆行列を計算し格納
			world.r[3] = XMVectorSet(0.f, 0.f, 0.f, 1.f);
			XMMATRIX inverse_world{ XMMatrixInverse(nullptr, world) };
			XMStoreFloat4x4(&inv_world[index], inverse_world);

			has_transform[index] = 1;// 計算済みに設定
		}

		/// @brief 回転ベクトルから向きを計算
		/// @param rotation 回転クォータニオン
		/// @return 向きのベクトル
		math::v3 calculate_orientation(math::v4 rotation)
		{
			using namespace DirectX;
			XMVECTOR rotation_quat{ XMLoadFloat4(&rotation) };
			XMVECTOR front{ XMVectorSet(0.f, 0.f, 1.f, 0.f) };
			math::v3 orientation;
			XMStoreFloat3(&orientation, XMVector3Normalize(XMVector3Rotate(front, rotation_quat)));
			return orientation;
		}

	} // 匿名名前空間

	void set_rotation(transform_id id, const math::v4& rotation_quaternion)
	{
		const u32 index{ id::index(id) };
		rotations[index] = rotation_quaternion;
		orientations[index] = calculate_orientation(rotation_quaternion);
		has_transform[index] = 0;
		changes_from_previous_frame[index] |= (u8)component_flags::rotation;
	}

	void set_orientation(transform_id id, const math::v3& orientation_vector)
	{
	}

	void set_position(transform_id id, const math::v3& position)
	{
		const u32 index{ id::index(id) };
		positions[index] = position;
		has_transform[index] = 0;
		changes_from_previous_frame[index] |= (u8)component_flags::position;
	}

	void set_scale(transform_id id, const math::v3& scale)
	{
		const u32 index{ id::index(id) };
		scales[index] = scale;
		has_transform[index] = 0;
		changes_from_previous_frame[index] |= (u8)component_flags::scale;
	}


	/// @brief Transformコンポーネントを作成
	/// @param info 初期化情報
	/// @param entity 関連付けるエンティティ
	/// @return 作成されたTransformコンポーネント
	component create(init_info info, game_entity::entity entity)
	{
		assert(entity.is_valid());
		const id::id_type entity_index{ id::index(entity.get_id()) };

		// エンティティのインデックスに基づいてデータを初期化または追加
		if (positions.size() > entity_index)
		{
			math::v4 rotation{ info.rotation };
			rotations[entity_index] = rotation;
			orientations[entity_index] = calculate_orientation(rotation);
			positions[entity_index] = math::v3{ info.position };
			scales[entity_index] = math::v3{ info.scale };
			has_transform[entity_index] = 0;
			changes_from_previous_frame[entity_index] = (u8)component_flags::all;
		}
		else
		{
			assert(positions.size() == entity_index);
			to_world.emplace_back();
			inv_world.emplace_back();
			rotations.emplace_back(info.rotation);
			orientations.emplace_back(calculate_orientation(math::v4{ info.rotation }));
			positions.emplace_back(info.position);
			scales.emplace_back(info.scale);
			has_transform.emplace_back((u8)0);
			changes_from_previous_frame.emplace_back((u8)component_flags::all);
		}

		// NOTE: 各エンティティはトランスフォームコンポーネントを持つ。
		//		したがって、トランスフォームコンポーネントのidは、エンティティのidとまったく同じです。
		// TransformコンポーネントのIDを返す
		return component{ transform_id{ entity.get_id() } };
	}

	/// @brief Transformコンポーネントを削除
	/// @param c 削除対象のコンポーネント
	void remove([[maybe_unused]] component c)
	{
		assert(c.is_valid());
	}

	/// @brief Transform行列を取得
	/// @param id エンティティID
	/// @param world 世界行列の参照
	/// @param inverse_world 逆行列の参照
	void get_transform_matrices(const game_entity::entity_id id, math::m4x4& world, math::m4x4& inverse_world)
	{
		assert(game_entity::entity{ id }.is_valid());

		const id::id_type entity_index{ id::index(id) };
		if (!has_transform[entity_index])
		{
			calculate_transform_matrices(entity_index);
		}

		world = to_world[entity_index];
		inverse_world = inv_world[entity_index];
	}

	void get_updated_component_flags(const game_entity::entity_id* const ids, u32 count, u8* const flags)
	{
		assert(ids && count && flags);
		read_write_flags = 1;

		for (u32 i{ 0 }; i < count; ++i)
		{
			assert(game_entity::entity{ ids[i] }.is_valid());
			flags[i] = changes_from_previous_frame[id::index(ids[i])];
		}
	}

	void update(const component_cache* const cache, u32 count)
	{
		assert(cache && count);

		// NOTE: "changes_from_previous_frame"をクリアするのは、読み込みがなく、
		//		この関数を呼び出すことで変更が適用されようとしているとき
		//		（つまり、現在のフレームの残りは書き込みのみ）に、フレームごとに一度だけ起こる。
		if (read_write_flags)
		{
			memset(changes_from_previous_frame.data(), 0, changes_from_previous_frame.size());
			read_write_flags = 0;
		}

		for (u32 i{ 0 }; i < count; ++i)
		{
			const component_cache& c{ cache[i] };
			assert(component{ c.id }.is_valid());

			if (c.flags & component_flags::rotation)
			{
				set_rotation(c.id, c.rotation);
			}
			if (c.flags & component_flags::orientation)
			{
				set_orientation(c.id, c.orientation);
			}
			if (c.flags & component_flags::position)
			{
				set_position(c.id, c.position);
			}
			if (c.flags & component_flags::scale)
			{
				set_scale(c.id, c.scale);
			}
		}

	}

	/// @brief コンポーネントの回転を取得
	/// @return 回転ベクトル
	math::v4 component::rotation() const
	{
		assert(is_valid());
		return rotations[id::index(_id)];
	}

	/// @brief コンポーネントの向きを取得
	/// @return 向きのベクトル
	math::v3 component::orientation() const
	{
		assert(is_valid());
		return orientations[id::index(_id)];
	}

	/// @brief コンポーネントの位置を取得
	/// @return 位置ベクトル
	math::v3 component::position() const
	{
		assert(is_valid());
		return positions[id::index(_id)];
	}

	/// @brief コンポーネントのスケールを取得
	/// @return スケールベクトル
	math::v3 component::scale() const
	{
		assert(is_valid());
		return scales[id::index(_id)];
	}
}
