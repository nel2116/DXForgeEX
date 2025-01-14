// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Transform.h]
// 作成日 : 2024/08/12
// 作成者 : 田中ミノル
// 概要
// 　Transformコンポーネントを定義したファイル
// 更新履歴
// 2024/08/12 新規作成
// 2025/01/14 コメントの追加
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "ComponentsCommon.h" // 共通コンポーネントヘッダーのインクルード

namespace dxforge::transform
{
	/// @brief Transformコンポーネント初期化情報を格納する構造体
	struct init_info
	{
		f32 position[3]{}; // 位置情報 (x, y, z)
		f32 rotation[4]{}; // 回転情報 (クォータニオン形式: x, y, z, w)
		f32 scale[3]{ 1.f, 1.f, 1.f }; // スケール情報 (デフォルトは1.0)
	};

	/// @brief 新しいTransformコンポーネントを作成
	/// @param info 初期化情報
	/// @param entity 関連付けるエンティティ
	/// @return 作成されたTransformコンポーネント
	component create(init_info info, game_entity::entity entity);

	/// @brief 指定されたTransformコンポーネントを削除
	/// @param c 削除するTransformコンポーネント
	void remove(component c);

	/// @brief エンティティのTransform行列を取得
	/// @param id エンティティID
	/// @param world 世界行列 (出力)
	/// @param inverse_world 世界行列の逆行列 (出力)
	void get_transform_matrices(const game_entity::entity_id id, math::m4x4& world, math::m4x4& inverse_world);
}
