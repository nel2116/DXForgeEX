// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Geometry.h]
// 作成日 : 2025/02/10
// 作成者 : 田中ミノル
// 概要
// ゲームエンティティのGeometryコンポーネントを管理する
// 更新履歴
// 2025/02/10 新規作成
// 2026/11/06 IDを指定してレンダーアイテムとエンティティを取得する関数を追加 (Primal Day 082, #201)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "ComponentsCommon.h"

namespace dxforge::geometry
{
	/// @brief Geometryコンポーネントの初期化情報
	struct init_info
	{
		id::id_type     geometry_content_id;	///< ジオメトリコンテンツのID
		u32             material_count;			///< マテリアルの数
		id::id_type* material_ids;			///< マテリアルのID
	};

	/// @brief Geometryコンポーネントの作成
	/// @param info 初期化情報
	/// @param entity アタッチするゲームエンティティ
	/// @return Geometryコンポーネント
	component create(init_info info, game_entity::entity entity);

	/// @brief Geometryコンポーネントの削除
	/// @param c 削除するGeometryコンポーネント
	void remove(component c);

	/// @brief Geometryコンポーネントの取得
	/// @param item_ids GeometryコンポーネントのID
	/// @param count 取得する数
	void get_render_item_ids(id::id_type* const item_ids, u32 count);

	/// @brief 指定したGeometryコンポーネントのレンダーアイテムのIDを取得する
	/// @param geometry_ids GeometryコンポーネントのID
	/// @param item_ids レンダーアイテムのIDを書き込む先
	/// @param count 取得する数
	void get_render_item_ids(const id::id_type* const geometry_ids, id::id_type* const item_ids, u32 count);

	/// @brief 指定したGeometryコンポーネントを持つエンティティのIDを取得する
	/// @param geometry_ids GeometryコンポーネントのID
	/// @param entity_ids エンティティのIDを書き込む先
	/// @param count 取得する数
	void get_entity_ids(const id::id_type* const geometry_ids, game_entity::entity_id* const entity_ids, u32 count);
}