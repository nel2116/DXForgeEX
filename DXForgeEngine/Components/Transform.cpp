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
// 2026/10/18 set_orientation()が未実装である旨をTODOとして明記
// 2026/10/18 set_orientation()を削除し、向きを回転から導出するローカルフレーム(right/up/front)で保持するよう変更 (#4)
// 2026/10/19 update()のassertで、キャッシュの所有エンティティが生きている(世代が一致する)ことまで確かめるよう変更 (#78)
// /_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Transform.h"
#include "Entity.h"

namespace dxforge::transform
{
	// 匿名名前空間に各種ベクターを定義
	namespace
	{
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4201)
#endif
		// C4201: nonstandard extension used: nameless struct/union
		/// @brief エンティティのローカル座標系(右・上・前方の各軸)を格納する構造体
		struct local_frame
		{
			union {
				struct {
					math::v3 right;	///< 右方向 (ローカルX軸)
					math::v3 up;	///< 上方向 (ローカルY軸)
					math::v3 front;	///< 前方向 (ローカルZ軸)
				};
				math::m3x3 frame;	///< 3軸を行として並べた3x3行列
			};

			local_frame()
				: right{ 1.f, 0.f, 0.f }, up{ 0.f, 1.f, 0.f }, front{ 0.f, 0.f, 1.f }
			{}
		};

#ifdef _MSC_VER
#pragma warning(pop)
#endif

		utl::vector<math::m4x4> to_world;				///< ワールド行列を格納するベクター
		utl::vector<math::m4x4> inv_world;				///< ワールド行列の逆行列を格納するベクター
		utl::vector<math::v4> rotations;				///< 各エンティティの回転情報を格納するベクター
		utl::vector<local_frame> local_frames;			///< 各エンティティのローカルフレームを格納するベクター
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

		/// @brief 回転クォータニオンからローカルフレームを計算
		/// @param rotation 回転クォータニオン
		/// @param result 計算結果を格納するローカルフレーム
		void calculate_local_frame(const math::v4& rotation, local_frame& result)
		{
			using namespace DirectX;

			local_frame frame{};

			XMVECTOR right{ XMLoadFloat3(&frame.right) };
			XMVECTOR up{ XMLoadFloat3(&frame.up) };
			XMVECTOR front{ XMLoadFloat3(&frame.front) };
			XMVECTOR rotation_quat{ XMLoadFloat4(&rotation) };

			// 右・上を回転させ、前方はその外積から求めて3軸の直交性を保つ
			right = XMVector3Normalize(XMVector3Rotate(right, rotation_quat));
			up = XMVector3Normalize(XMVector3Rotate(up, rotation_quat));
			front = XMVector3Normalize(XMVector3Cross(right, up));

			XMStoreFloat3(&result.right, right);
			XMStoreFloat3(&result.up, up);
			XMStoreFloat3(&result.front, front);
		}

	} // 匿名名前空間

	void set_rotation(transform_id id, const math::v4& rotation_quaternion)
	{
		const u32 index{ id::index(id) };
		rotations[index] = rotation_quaternion;
		calculate_local_frame(rotation_quaternion, local_frames[index]);
		has_transform[index] = 0;
		changes_from_previous_frame[index] |= (u8)component_flags::rotation;
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
			calculate_local_frame(rotation, local_frames[entity_index]);
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
			local_frames.emplace_back();
			calculate_local_frame(rotations.back(), local_frames.back());
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
			// NOTE: IDが無効値でないことだけでなく、所有するエンティティが生きている(世代が一致する)ことまで確かめる。
			//		 削除済みのエンティティのキャッシュを渡すと、死んだスロットや、同じインデックスを再利用した別のエンティティに書き込んでしまう(#78)。
			assert(component{ c.id }.is_valid() && game_entity::is_alive(game_entity::entity_id{ (id::id_type)c.id }));

			if (c.flags & component_flags::rotation)
			{
				set_rotation(c.id, c.rotation);
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

	/// @brief コンポーネントの右方向(ローカルX軸)を取得
	/// @return 右方向の単位ベクトル
	math::v3 component::right() const
	{
		assert(is_valid());
		return local_frames[id::index(_id)].right;
	}

	/// @brief コンポーネントの上方向(ローカルY軸)を取得
	/// @return 上方向の単位ベクトル
	math::v3 component::up() const
	{
		assert(is_valid());
		return local_frames[id::index(_id)].up;
	}

	/// @brief コンポーネントの前方向(ローカルZ軸)を取得
	/// @return 前方向の単位ベクトル
	math::v3 component::front() const
	{
		assert(is_valid());
		return local_frames[id::index(_id)].front;
	}

	/// @brief コンポーネントのローカルフレームを取得
	/// @return 右・上・前方向を行として並べた3x3行列
	math::m3x3 component::local_frame() const
	{
		assert(is_valid());
		return local_frames[id::index(_id)].frame;
	}
}
