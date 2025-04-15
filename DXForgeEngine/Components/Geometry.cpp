// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Geometry.cpp]
// 作成日 : 2025/02/10
// 作成者 : 田中ミノル
// 概要
// ゲームエンティティのGeometryコンポーネントを管理する
// 更新履歴
// 2025/02/10 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Geometry.h"
#include "Entity.h"
#include "Graphics/Renderer.h"

namespace dxforge::geometry
{
	namespace
	{
		utl::vector<u32>                    active_lod;			///< 有効化されているLOD
		utl::vector<id::id_type>            render_item_ids;	///< レンダーアイテムのID
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
		active_lod.emplace_back(0);
		render_item_ids.emplace_back(graphics::add_render_item(entity.get_id(), info.geometry_content_id, info.material_count, info.material_ids));
		owner_ids.emplace_back(id::index(id));
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
		utl::erase_unordered(active_lod, index);
		utl::erase_unordered(render_item_ids, index);
		utl::erase_unordered(owner_ids, index);
		id_mapping[id::index(last_id)] = index;
		id_mapping[id::index(id)] = id::invalid_id;

		if (generations[index] < id::max_generation)
		{
			free_ids.push_back(id);
		}
	}

	void get_render_item_ids(id::id_type* const item_ids, u32 count)
	{
		assert(render_item_ids.size() >= count);
		memcpy(item_ids, render_item_ids.data(), count * sizeof(id::id_type));
	}

}