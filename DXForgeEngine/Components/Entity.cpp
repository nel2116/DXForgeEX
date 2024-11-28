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
#include "Transform.h"
#include "Script.h"

namespace dxforge::game_entity
{
	namespace
	{
		utl::vector<transform::component> transforms;
		utl::vector<script::component> scripts;
		utl::vector<id::generation_type> generations;
		utl::deque<entity_id> free_ids;
	}	// anonymous namespace

	entity create(entity_info info)
	{
		assert(info.transform);									// すべてのゲーム・エンティティは、トランスフォーム・コンポーネントを持たなければならない。
		if (!info.transform) return entity();					// transform 情報が nullptr の場合は無効なエンティティを返す

		entity_id id;											// エンティティIDを格納する変数

		// 削除されたエンティティIDがあるかどうかを判定
		if (free_ids.size() > id::min_deleted_elements)
		{	// 削除されたエンティティIDがある場合
			id = free_ids.front();								// 削除されたエンティティIDを再利用
			assert(!is_alive(id));					// 再利用したIDが使われていないことを確認
			free_ids.pop_front();								// 再利用したIDを削除
			id = entity_id{ id::new_generation(id) };			// 世代番号を更新
			++generations[id::index(id)];						// 世代番号を更新
		}
		else
		{	// 削除されたエンティティIDがない場合
			id = entity_id{ (id::id_type)generations.size() };	// 新しいエンティティIDを作成
			generations.push_back(0);							// 世代番号を初期化

			// Resize components
			// NOTE : resize()を呼び出さないので、メモリ割り当ての回数が少ない。
			transforms.emplace_back();							// Transformコンポーネントを追加
		}

		const entity new_entity{ id };							// 新しいエンティティを作成
		const id::id_type index{ id::index(id) };				// インデックスを取得

		// Transformコンポーネントの作成
		assert(!transforms[index].is_valid());					// 有効なTransformコンポーネントであることを確認
		transforms[index] = transform::create(*info.transform, new_entity);	// Transformコンポーネントを作成
		if (!transforms[index].is_valid()) return {};	// Transformコンポーネントが無効な場合は無効なエンティティを返す

		// Scriptコンポーネントの作成
		if (info.script && info.script->script_creator)
		{
			assert(!scripts[index].is_valid());
			scripts[index] = script::create(*info.script, new_entity);
			assert(scripts[index].is_valid());
		}

		return new_entity;										// 新しいエンティティを返す
	}

	void remove(entity_id id)
	{
		const id::id_type index{ id::index(id) };
		assert(is_alive(id));
		transform::remove(transforms[index]);
		transforms[index] = {};		// Transformコンポーネントを削除
		free_ids.push_back(id);
	}

	bool is_alive(entity_id id)
	{
		assert(id::is_valid(id));								// 有効なエンティティであることを確認
		const id::id_type index{ id::index(id) };
		assert(index < generations.size());					// インデックスが範囲内であることを確認
		assert(generations[index] == id::generation(id));
		return (generations[index] == id::generation(id) && transforms[index].is_valid());
	}

	transform::component entity::transform() const
	{
		assert(is_alive(_id));
		const id::id_type index{ id::index(_id) };
		return transforms[index];
	}

	script::component entity::script() const
	{
		assert(is_alive(_id));
		const id::id_type index{ id::index(_id) };
		return scripts[index];
	}
}