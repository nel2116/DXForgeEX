// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Entity.cpp]
// 作成日 : 2024/08/12
// 作成者 : 田中ミノル
// 概要
// 	エンティティを実装したファイル
// 更新履歴
// 2024/08/12 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Entity.h"

namespace dxforge::game_entity
{
	namespace
	{
		utl::vector<id::generation_type> generations;
		utl::deque<entity_id> free_ids;
	}

	entity create_game_entity(const entity_info& info)
	{
		assert(info.transform);									// transform 情報が nullptr でないことを確認
		if (!info.transform) return entity();					// transform 情報が nullptr の場合は無効なエンティティを返す

		entity_id id;											// エンティティIDを格納する変数

		// 削除されたエンティティIDがあるかどうかを判定
		if (free_ids.size() > id::min_deleted_elements)
		{	// 削除されたエンティティIDがある場合
			id = free_ids.front();								// 削除されたエンティティIDを再利用
			assert(!is_alive(entity{ id }));					// 再利用したIDが使われていないことを確認
			free_ids.pop_front();								// 再利用したIDを削除
			id = entity_id{ id::new_generation(id) };			// 世代番号を更新
			++generations[id::index(id)];						// 世代番号を更新
		}
		else
		{	// 削除されたエンティティIDがない場合
			id = entity_id{ (id::id_type)generations.size() };	// 新しいエンティティIDを作成
			generations.push_back(0);							// 世代番号を初期化
		}

		const entity new_entity{ id };							// 新しいエンティティを作成
		const id::id_type index{ id::index(id) };				// インデックスを取得

		return new_entity;										// 新しいエンティティを返す
	}

	void remove_game_entity(entity id)
	{
	}

	bool is_alive(entity id)
	{
		return false;
	}

}