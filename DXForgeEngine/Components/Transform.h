// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Transform.h]
// 作成日 : 2024/08/12
// 作成者 : 田中ミノル
// 概要
// 　Transformコンポーネントを定義したファイル
// 更新履歴
// 2024/08/12 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "ComponentsCommon.h"

namespace dxforge::transform
{
	DEFINE_TYPED_ID(transform_id);

	struct init_info
	{
		f32 positon[3]{};        // 位置情報を格納する3次元ベクトル（x, y, z）
		f32 rotation[4]{};       // 回転情報を格納するクォータニオン（w, x, y, z）
		f32 scale[3]{ 1.f, 1.f, 1.f }; // スケール情報を格納する3次元ベクトル（x, y, z）、デフォルトは全軸で1.0
	};

	// Transformコンポーネントを作成する関数
	transform_id create_transform(const init_info& info, game_entity::entity_id entity_id);
	// Transformコンポーネントを削除する関数
	void remove_transform(transform_id id);
}