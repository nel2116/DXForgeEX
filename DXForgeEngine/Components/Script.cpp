// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Script.cpp]
// 作成日 : 2024/12/2
// 作成者 : 田中ミノル
// 概要
// 　スクリプトを実装したファイル
// 更新履歴
// 2024/12/2 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Script.h"
#include "Entity.h"
#include "Transform.h"

#define USE_TRANSFORM_CACHE_MAP 1

namespace dxforge::script
{
	namespace
	{
		utl::vector<detail::script_ptr> entity_scripts;				///< エンティティスクリプトのベクター
		utl::vector<id::id_type> id_mapping;						///< IDマッピング
		utl::vector<id::generation_type> generations;				///< 世代情報
		utl::deque<script_id> free_ids;								///< 解放されたID

		utl::vector<transform::component_cache> transform_cache;	///< Transformコンポーネントのキャッシュ
#if USE_TRANSFORM_CACHE_MAP
		std::unordered_map<id::id_type, u32> cache_map;				///< キャッシュマップ
#endif // !USE_TRANSFORM_CACHE_MAP

		using script_registry = std::unordered_map<size_t, detail::script_creator>;

		script_registry& registery()
		{
			// NOTE : この静的変数を関数内に置くのは、次の理由からである。
			//		  静的データの初期化順序
			//		  こうすることでアクセスする前にデータが初期化されていることを確認できる。
			static script_registry reg;
			return reg;
		}

#ifdef USE_WITH_EDITOR
		utl::vector<std::string>& script_names()
		{
			// NOTE : この静的変数を関数内に置くのは、次の理由からである。
			//		  静的データの初期化順序
			//		  こうすることでアクセスする前にデータが初期化されていることを確認できる。
			static utl::vector<std::string> names;
			return names;
		}
#endif // USE_WITH_EDITOR

		/// @brief IDが存在するかどうかを返す
		/// @param id ID
		/// @return 存在する場合はtrue
		bool exists(script_id id)
		{
			assert(id::is_valid(id));
			const id::id_type index{ id::index(id) };
			assert(index < generations.size() && !(id::is_valid(id_mapping[index]) && id_mapping[index] >= entity_scripts.size()));
			assert(generations[index] == id::generation(id));
			return (id::is_valid(id_mapping[index]) && generations[index] == id::generation(id)) && entity_scripts[id_mapping[index]] && entity_scripts[id_mapping[index]]->is_valid();
		}

#if USE_TRANSFORM_CACHE_MAP
		transform::component_cache* get_cache_ptr(const game_entity::entity* const entity)
		{
			assert(game_entity::is_alive((+entity)->get_id()));
			const transform::transform_id id{ (*entity).transform().get_id() };

			u32 index{ u32_invalid_id };
			auto pair = cache_map.try_emplace(id, id::invalid_id);

			// cache_mapにこのidのエントリーがなかったため、新しいエントリーを挿入した。
			if (pair.second)
			{
				index = (u32)transform_cache.size();
				transform_cache.emplace_back();
				transform_cache.back().id = id;
				cache_map[id] = index;
			}
			else
			{
				index = cache_map[id];
			}

			assert(index < transform_cache.size());
			return &transform_cache[index];
		}
#else
		transform::component_cache* const get_cache_ptr(const game_entity::entity* const entity)
		{
			assert(game_entity::is_alive((*entity).get_id()));
			const transform::transform_id id{ (*entity).transform().get_id() };

			for (auto& cache : transform_cache)
			{
				if (cache.id == id)
				{
					return &cache;
				}
			}

			transform_cache.emplace_back();
			transform_cache.back().id = id;

			return &transform_cache.back();
		}
#endif // !USE_TRANSFORM_CACHE_MAP

	}// 匿名名前空間

	namespace detail
	{
		u8 register_script(size_t tag, script_creator func)
		{
			bool result{ registery().insert(script_registry::value_type{tag,func}).second };
			assert(result);
			return result;
		}

		script_creator get_script_creator(size_t tag)
		{
			auto script = dxforge::script::registery().find(tag);
			assert(script != dxforge::script::registery().end() && script->first == tag);
			return script->second;
		}

#ifdef USE_WITH_EDITOR
		u8 add_script_name(const char* name)
		{
			script_names().emplace_back(name);
			return true;
		}

#endif // USE_WITH_EDITOR


	}	// namespace detail

	component create(init_info info, game_entity::entity entity)
	{
		assert(entity.is_valid());
		assert(info.script_creator);

		script_id id{};
		if (free_ids.size() > id::min_deleted_elements)
		{
			id = free_ids.front();
			assert(!exists(id));
			free_ids.pop_front();
			id = script_id{ id::new_generation(id) };
			++generations[id::index(id)];
		}
		else
		{
			id = script_id{ (id::id_type)id_mapping.size() };
			id_mapping.emplace_back();
			generations.push_back(0);
		}

		assert(id::is_valid(id));
		const id::id_type index{ (id::id_type)entity_scripts.size() };
		entity_scripts.emplace_back(info.script_creator(entity));
		assert(entity_scripts.back()->get_id() == entity.get_id());
		id_mapping[id::index(id)] = index;
		return component{ id };
	}

	void remove(component c)
	{
		assert(c.is_valid() && exists(c.get_id()));
		const script_id id{ c.get_id() };
		const id::id_type index{ id_mapping[id::index(id)] };
		const script_id last_id{ entity_scripts.back()->script().get_id() };
		utl::erase_unordered(entity_scripts, index);
		id_mapping[id::index(last_id)] = index;
		id_mapping[id::index(id)] = id::invalid_id;

		if (generations[index] < id::max_generation)
		{
			free_ids.push_back(id);
		}
	}

	void update(f32 dt)
	{
		for (const auto& ptr : entity_scripts)
		{
			ptr->update(dt);
		}

		if (transform_cache.size())
		{
			transform::update(transform_cache.data(), (u32)transform_cache.size());
			transform_cache.clear();

#if USE_TRANSFORM_CACHE_MAP
			cache_map.clear();
#endif // !USE_TRANSFORM_CACHE_MAP
		}
	}

	void entity_script::set_rotation(const game_entity::entity* const entity, math::v4 rotation_quaretnion)
	{
		transform::component_cache& cache{ *get_cache_ptr(entity) };
		cache.flags |= transform::component_flags::rotation;
		cache.rotation = rotation_quaretnion;
	}

	void entity_script::set_orientation(const game_entity::entity* const entity, math::v3 orientation_vector)
	{
		transform::component_cache& cache{ *get_cache_ptr(entity) };
		cache.flags |= transform::component_flags::orientation;
		cache.orientation = orientation_vector;
	}

	void entity_script::set_position(const game_entity::entity* const entity, math::v3 position)
	{
		transform::component_cache& cache{ *get_cache_ptr(entity) };
		cache.flags |= transform::component_flags::position;
		cache.position = position;
	}

	void entity_script::set_scale(const game_entity::entity* const entity, math::v3 scale)
	{
		transform::component_cache& cache{ *get_cache_ptr(entity) };
		cache.flags |= transform::component_flags::scale;
		cache.scale = scale;
	}

}	// namespace dxforge::script

#ifdef USE_WITH_EDITOR
#include <atlsafe.h>

extern "C" __declspec(dllexport)
LPSAFEARRAY get_script_names()
{
	const u32 size{ (u32)dxforge::script::script_names().size() };
	if (!size) return nullptr;
	CComSafeArray<BSTR> names{ size };
	for (u32 i{ 0 }; i < size; ++i)
	{
		names.SetAt(i, A2BSTR_EX(dxforge::script::script_names()[i].c_str()), false);
	}
	return names.Detach();
}

#endif // USE_WITH_EDITOR
