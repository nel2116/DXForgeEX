// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Script.cpp]
// 作成日 : 2024/12/2
// 作成者 : 田中ミノル
// 概要
// 　スクリプトを実装したファイル
// 更新履歴
// 2024/12/2 新規作成
// 2026/10/18 remove()の世代チェックが密なインデックスでgenerationsを参照していたのを修正 (#21)
// 2026/10/18 set_orientation()を削除し、向きを回転から導出するローカルフレーム(right/up/front)で保持するよう変更 (#4)
// 2026/10/18 begin_play()が呼ばれていなかったのを修正し、生成後最初のupdate()の前に1回だけ呼ぶよう変更 (#47)
// 2026/10/18 削除したスクリプトをbegin_play()の待ちリストから外すよう修正(エディタで待ちリストが増え続けるため) (#47)
// 2026/10/18 update()の中でスクリプトを生成・削除するとイテレータが無効になるのを修正(添字で回し、削除はupdate()の最後にまとめて行う) (#54)
// 2026/10/18 update()中に削除したスクリプトもremove()の中で破棄し、実行中のスクリプト自身の削除だけを遅らせるよう変更(デストラクタからTransformを読めるようにするため) (#54)
// 2026/10/19 update()の中で削除されたエンティティのTransformキャッシュを、適用する前に取り除くよう修正(削除済み・再利用されたスロットに書き込んでいたため) (#78)
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
		utl::vector<script_id> pending_begin_play;					///< begin_play()をまだ呼んでいないスクリプトのID
		utl::vector<detail::script_ptr> removed_scripts;			///< 実行中に自分自身を削除し、update()の最後での破棄を待っているスクリプト
		const entity_script* executing_script{ nullptr };			///< begin_play()/update()を実行中のスクリプト
		bool is_updating{ false };									///< update()の実行中かどうか

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

		u32 pending_begin_play_count()
		{
			return (u32)pending_begin_play.size();
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

		// NOTE: begin_play()はここでは呼ばず、次のupdate()の冒頭で呼ぶ。
		// エディタはゲームDLLを読み込んだ状態でエンティティを生成するため、ここで呼ぶとエディタ内でゲームコードが動いてしまう。
		pending_begin_play.emplace_back(id);
		return component{ id };
	}

	void remove(component c)
	{
		assert(c.is_valid() && exists(c.get_id()));
		const script_id id{ c.get_id() };
		const id::id_type index{ id_mapping[id::index(id)] };
		if (is_updating)
		{
			// NOTE: update()の実行中にerase_unordered()で要素を入れ替えると、ループで飛ばされる・二重に呼ばれるスクリプトが出る。
			//		 そのため、ここではスロットを空にするだけにして、update()の最後に詰める。
			//		 スクリプトは、デストラクタからTransformなどを読めるよう、エンティティが削除される前(ここ)で破棄する。
			//		 ただし、実行中のスクリプトが自分自身を削除した場合は、実行中に破棄できないのでupdate()の最後まで遅らせる。
			//		 破棄する前にスロットから取り出すので、デストラクタの中で生成・削除が行われても壊れない。
			detail::script_ptr removed{ std::move(entity_scripts[index]) };
			if (removed.get() == executing_script)
			{
				removed_scripts.emplace_back(std::move(removed));
			}
		}
		else
		{
			const script_id last_id{ entity_scripts.back()->script().get_id() };
			utl::erase_unordered(entity_scripts, index);
			id_mapping[id::index(last_id)] = index;
		}
		id_mapping[id::index(id)] = id::invalid_id;

		// NOTE: エディタはscript::update()を呼ばないため、ここで待ちリストから外さないと、
		//		 ゲームDLLを読み直すたびに削除済みのIDがたまり続ける。
		//		 begin_play()を呼ぶ順番を変えないよう、erase_unorderedではなくeraseを使う。
		for (u32 i{ 0 }; i < pending_begin_play.size(); ++i)
		{
			if (pending_begin_play[i] == id)
			{
				pending_begin_play.erase(pending_begin_play.begin() + i);
				break;
			}
		}

		// NOTE: generationsは疎なIDのインデックス(id::index(id))で引く。indexはentity_scripts上の密なインデックスなので使えない。
		if (generations[id::index(id)] < id::max_generation)
		{
			free_ids.push_back(id);
		}
	}

	void update(f32 dt)
	{
		assert(!is_updating);
		is_updating = true;

		// NOTE: begin_play()の中でスクリプトが生成・削除されることがある(削除はremove()が待ちリストを詰める)ため、
		//		 その時点の待ちリストを取り出してから処理する。begin_play()の中で生成された分は待ちリストに積まれるので、
		//		 空になるまで繰り返して同じフレームで処理する。取り出した後に削除されたスクリプトは、id_mappingと世代で判定して飛ばす。
		while (!pending_begin_play.empty())
		{
			const utl::vector<script_id> pending{ std::move(pending_begin_play) };
			for (const script_id id : pending)
			{
				const id::id_type index{ id::index(id) };
				if (id::is_valid(id_mapping[index]) && generations[index] == id::generation(id))
				{
					executing_script = entity_scripts[id_mapping[index]].get();
					entity_scripts[id_mapping[index]]->begin_play();
				}
			}
		}

		// NOTE: update()の中でスクリプトが生成されるとentity_scriptsが再確保されるため、範囲forではなく添字で回す。
		//		 生成されたスクリプトは末尾に積まれ、次のupdate()でbegin_play()を呼んでからupdate()を呼ぶため(#47)、
		//		 ループの前の数だけ回す。削除されたスクリプトのスロットは空になっているので飛ばす。
		const u32 count{ (u32)entity_scripts.size() };
		for (u32 i{ 0 }; i < count; ++i)
		{
			if (entity_scripts[i])
			{
				executing_script = entity_scripts[i].get();
				entity_scripts[i]->update(dt);
			}
		}

		executing_script = nullptr;
		is_updating = false;

		// update()の実行中に削除されたスクリプトの空きスロットを、末尾の要素で埋めて詰める。
		// 後ろから見ていくので、末尾の要素は常に空ではない。
		for (u32 i{ (u32)entity_scripts.size() }; i > 0;)
		{
			--i;
			if (entity_scripts[i]) continue;

			if (i != entity_scripts.size() - 1)
			{
				const script_id last_id{ entity_scripts.back()->script().get_id() };
				id_mapping[id::index(last_id)] = i;
			}
			utl::erase_unordered(entity_scripts, i);
		}

		// NOTE: 破棄するスクリプトのデストラクタがさらにエンティティを削除しても壊れないよう、取り出してから破棄する。
		{
			utl::vector<detail::script_ptr> removed{ std::move(removed_scripts) };
		}

		if (transform_cache.size())
		{
			// NOTE: キャッシュを積んだ後、同じフレームの中で削除されたエンティティのキャッシュを取り除く(#78)。
			//		 削除はscript::update()の中のどこからでも(他のスクリプトや、上で破棄したスクリプトのデストラクタからも)起こりうるため、
			//		 削除時にキャッシュを外すのではなく、適用する直前に世代まで見て判定する。
			//		 そのまま渡すと、死んだスロットや、同じインデックスを再利用して生成された別のエンティティのTransformを上書きしてしまう。
			for (u32 i{ (u32)transform_cache.size() }; i > 0;)
			{
				--i;
				if (!game_entity::is_alive(game_entity::entity_id{ (id::id_type)transform_cache[i].id }))
				{
					utl::erase_unordered(transform_cache, i);
				}
			}

			if (transform_cache.size())
			{
				transform::update(transform_cache.data(), (u32)transform_cache.size());
			}
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
