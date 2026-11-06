// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Geometry.cpp]
// 作成日 : 2025/02/10
// 作成者 : 田中ミノル
// 概要
// ゲームエンティティのGeometryコンポーネントを管理する
// 更新履歴
// 2025/02/10 新規作成
// 2026/10/18 remove()の世代チェックが密なインデックスでgenerationsを参照していたのを修正 (#21)
// 2026/11/06 active_lod を削除し、エンティティのIDの保持と、IDを指定する取得の関数を追加 (Primal Day 082, #201)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Geometry.h"
#include "Entity.h"
#include "Graphics/Renderer.h"

namespace dxforge::geometry
{
	namespace
	{
		utl::vector<id::id_type>            render_item_ids;	///< レンダーアイテムのID
		utl::vector<game_entity::entity_id> owning_entity_ids;	///< コンポーネントを持つエンティティのID
		utl::vector<geometry_id>            owner_ids;			///< オーナーのID
		utl::vector<id::id_type>            id_mapping;			///< IDマッピング

		utl::vector<id::generation_type>    generations;		///< 世代
		utl::deque<geometry_id>             free_ids;			///< 解放されたID

#if _DEBUG
		/// @brief IDが存在するかどうか
		/// @param id ID
		/// @return 存在するかどうか
		bool exists(geometry_id id)
		{
			assert(id::is_valid(id));
			const id::id_type index{ id::index(id) };
			assert(index < generations.size() && !(id::is_valid(id_mapping[index]) && id_mapping[index] >= render_item_ids.size()));
			assert(generations[index] == id::generation(id));
			return (generations[index] == id::generation(id)) && id::is_valid(id_mapping[index]) && id::is_valid(render_item_ids[id_mapping[index]]);
		}
#endif
	} // 匿名名前空間

	component create(init_info info, game_entity::entity entity)
	{
		assert(entity.is_valid());
		assert(id::is_valid(info.geometry_content_id) && info.material_count && info.material_ids);

		geometry_id id{};
		if (free_ids.size() > id::min_deleted_elements)
		{
			id = free_ids.front();
			assert(!exists(id));
			free_ids.pop_front();
			id = geometry_id{ id::new_generation(id) };
			++generations[id::index(id)];
		}
		else
		{
			id = geometry_id{ (id::id_type)id_mapping.size() };
			id_mapping.emplace_back();
			generations.push_back(0);
		}

		assert(id::is_valid(id));
		const id::id_type index{ (id::id_type)render_item_ids.size() };
		render_item_ids.emplace_back(graphics::add_render_item(entity.get_id(), info.geometry_content_id, info.material_count, info.material_ids));
		owner_ids.emplace_back(id::index(id));
		owning_entity_ids.emplace_back(entity.get_id());
		id_mapping[id::index(id)] = index;
		return component{ id };
	}

	void remove(component c)
	{
		assert(c.is_valid() && exists(c.get_id()));
		const geometry_id id{ c.get_id() };
		const id::id_type index{ id_mapping[id::index(id)] };
		const geometry_id last_id{ owner_ids.back() };
		graphics::remove_render_item(render_item_ids[index]);
		utl::erase_unordered(render_item_ids, index);
		utl::erase_unordered(owning_entity_ids, index);
		utl::erase_unordered(owner_ids, index);
		id_mapping[id::index(last_id)] = index;
		id_mapping[id::index(id)] = id::invalid_id;

		// NOTE: generationsは疎なIDのインデックス(id::index(id))で引く。indexはrender_item_ids上の密なインデックスなので使えない。
		if (generations[id::index(id)] < id::max_generation)
		{
			free_ids.push_back(id);
		}
	}

	void get_render_item_ids(id::id_type* const item_ids, u32 count)
	{
		assert(item_ids && render_item_ids.size() >= count);
		memcpy(item_ids, render_item_ids.data(), count * sizeof(id::id_type));
	}

	void get_render_item_ids(const id::id_type* const geometry_ids, id::id_type* const item_ids, u32 count)
	{
		assert(geometry_ids && item_ids && count);
		assert(render_item_ids.size() >= count);
		for (u32 i{ 0 }; i < count; ++i)
		{
			const geometry_id id{ geometry_ids[i] };
			assert(id::is_valid(id) && exists(id));
			const id::id_type index{ id_mapping[id::index(id)] };
			assert(index < render_item_ids.size() && id::is_valid(render_item_ids[index]));
			item_ids[i] = render_item_ids[index];
		}
	}

	void get_entity_ids(const id::id_type* const geometry_ids, game_entity::entity_id* const entity_ids, u32 count)
	{
		assert(geometry_ids && entity_ids && count);
		for (u32 i{ 0 }; i < count; ++i)
		{
			const geometry_id id{ geometry_ids[i] };
			assert(id::is_valid(id) && exists(id));
			const id::id_type index{ id_mapping[id::index(id)] };
			assert(index < owning_entity_ids.size() && id::is_valid(owning_entity_ids[index]));
			entity_ids[i] = owning_entity_ids[index];
		}
	}

}