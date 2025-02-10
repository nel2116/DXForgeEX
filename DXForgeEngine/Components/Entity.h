// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Entity.h]
// 作成日 : 2024/08/12
// 作成者 : 田中ミノル
// 概要
// 	エンティティを定義したファイル
// 更新履歴
// 2024/08/12 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "ComponentsCommon.h"

namespace dxforge
{
	// INIT_INFO マクロを定義：指定された component の名前空間に init_info 構造体を定義する
#define INIT_INFO(component) namespace component { struct init_info; }

	// transform 名前空間に init_info 構造体を定義
	INIT_INFO(transform);
	INIT_INFO(script);
	INIT_INFO(geometry);

#undef INIT_INFO

	namespace game_entity {
		// entity_info 構造体を定義：ゲームエンティティの初期化情報を格納
		struct entity_info
		{
			// transform 名前空間の init_info 型のポインタを宣言、初期化は nullptr
			transform::init_info* transform{ nullptr };
			script::init_info* script{ nullptr };
			geometry::init_info* geometry{ nullptr };
		};

		// ゲームエンティティを作成する関数
		entity create(entity_info info);

		// ゲームエンティティを削除する関数
		void remove(entity_id id);

		// 指定したエンティティが有効かどうかを確認する関数
		bool is_alive(entity_id id);
	}
};
