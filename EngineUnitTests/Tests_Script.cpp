// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_Script.cpp]
// 作成日 : 2026/10/18
// 作成者 : 田中ミノル
// 概要 :
// Components/Script.cppのスクリプト登録・生成・削除・更新ロジックのテスト
// 更新履歴
// 2026/10/18 新規作成
// 2026/10/18 世代が上限に達したスクリプトIDを再利用しないことのテストを追加 (#21)
// 2026/10/18 begin_play()がupdate()より前に1回だけ呼ばれることのテストを追加 (#47)
// 2026/10/18 削除したスクリプトがbegin_play()の待ちリストに残らないことのテストを追加 (#47)
// 2026/10/18 update()/begin_play()の中でスクリプト付きエンティティを生成・削除するテストを追加 (#54)
// 2026/10/18 update()/begin_play()の中で削除されたスクリプトのデストラクタからTransformを読めることのテストを追加 (#54)
// 2026/10/19 update()の中でset_position()したエンティティを削除するテストと、同じフレームでIDを再利用したエンティティのTransformが上書きされないことのテストを追加 (#78)
// 2026/10/27 未登録のタグでget_script_creator()がnullptrを返すことのテストを追加 (#113)
// 2026/11/02 エディタ向けのスクリプト名のSAFEARRAY(make_script_names_safe_array())の要素数・中身・解放と、エディタのANSIマーシャリングとの往復のテストを追加(名前はACPで往復できる文字を選ぶ) (#146)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"
#include "CommonHeaders.h"
#include "Components/Entity.h"
#include "Components/Transform.h"
#include "Components/Script.h"
#include <functional>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdio>
#include <algorithm>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <oleauto.h>

using namespace dxforge;

namespace
{
	// NOTE: ECSの状態はプロセス全体で共有されるため、カウンタは絶対値ではなく
	// テスト前後の差分で検証する。
	struct script_counters
	{
		u32 created{ 0 };
		u32 destroyed{ 0 };
		u32 updated{ 0 };
		u32 begun{ 0 };
		u32 updated_before_begin_play{ 0 };
		f32 total_dt{ 0.f };
		game_entity::entity_id last_updated_id{ id::invalid_id };
	};

	script_counters counters{};

	// エンティティごとのupdate()の呼び出し回数(script_test_counterが記録する)
	std::unordered_map<id::id_type, u32> update_counts{};

	// 呼び出し回数を記録するだけのテスト用スクリプト
	class script_test_counter : public script::entity_script
	{
	public:
		explicit script_test_counter(game_entity::entity entity)
			: script::entity_script{ entity }
		{
			++counters.created;
		}

		~script_test_counter() override
		{
			++counters.destroyed;
		}

		void begin_play() override
		{
			++counters.begun;
			++_begin_play_count;
		}

		void update(f32 dt) override
		{
			if (_begin_play_count != 1) ++counters.updated_before_begin_play;
			++counters.updated;
			counters.total_dt += dt;
			counters.last_updated_id = get_id();
			++update_counts[get_id()];
		}

	private:
		u32 _begin_play_count{ 0 };
	};

	// update内でset_position()を呼び、Transformキャッシュ経由の反映を検証するスクリプト
	class script_test_mover : public script::entity_script
	{
	public:
		explicit script_test_mover(game_entity::entity entity)
			: script::entity_script{ entity } {}

		void update(f32 dt) override
		{
			set_position(math::v3{ dt, 2.f, 3.f });
		}
	};

	// begin_play()の中で、指定されたエンティティを削除するスクリプト
	game_entity::entity_id entity_to_remove_in_begin_play{ id::invalid_id };

	class script_test_remover : public script::entity_script
	{
	public:
		explicit script_test_remover(game_entity::entity entity)
			: script::entity_script{ entity } {}

		void begin_play() override
		{
			if (id::is_valid(entity_to_remove_in_begin_play))
			{
				game_entity::remove(entity_to_remove_in_begin_play);
				entity_to_remove_in_begin_play = game_entity::entity_id{ id::invalid_id };
			}
		}
	};

	// begin_play()/update()の中で、テストが指定した処理を行うスクリプト
	std::function<void(game_entity::entity_id)> begin_play_hook{};
	std::function<void(game_entity::entity_id)> update_hook{};

	class script_test_hook : public script::entity_script
	{
	public:
		explicit script_test_hook(game_entity::entity entity)
			: script::entity_script{ entity } {}

		// NOTE: フックは1回だけ呼ぶ。フックの中で自分自身が削除されることがあるため、
		//		 フックを取り出してから呼び、呼んだ後はメンバーに触れない。
		void begin_play() override
		{
			const auto hook{ std::move(begin_play_hook) };
			begin_play_hook = nullptr;
			if (hook) hook(get_id());
		}

		void update(f32) override
		{
			const auto hook{ std::move(update_hook) };
			update_hook = nullptr;
			if (hook) hook(get_id());
		}
	};

	// デストラクタで、自分のエンティティがまだ生きているか、位置を読めるかを記録するスクリプト
	struct reader_result
	{
		u32 destroyed{ 0 };
		u32 alive_in_destructor{ 0 };
		math::v3 position_in_destructor{};
	};
	reader_result reader{};

	class script_test_reader : public script::entity_script
	{
	public:
		explicit script_test_reader(game_entity::entity entity)
			: script::entity_script{ entity } {}

		~script_test_reader() override
		{
			++reader.destroyed;
			// NOTE: 生きていないエンティティのposition()はDebugではassertのダイアログで止まるため、先に確かめる。
			if (game_entity::is_alive(get_id()))
			{
				++reader.alive_in_destructor;
				reader.position_in_destructor = position();
			}
		}
	};

	// update()の中でset_position()を呼んでから、テストが指定した処理を行うスクリプト
	constexpr f32 moved_position{ 9.f };
	std::function<void(game_entity::entity_id)> after_move_hook{};

	class script_test_move_then_hook : public script::entity_script
	{
	public:
		explicit script_test_move_then_hook(game_entity::entity entity)
			: script::entity_script{ entity } {}

		// NOTE: フックの中で自分自身が削除されることがあるため、フックを取り出してから呼び、呼んだ後はメンバーに触れない。
		void update(f32) override
		{
			set_position(math::v3{ moved_position, moved_position, moved_position });
			const auto hook{ std::move(after_move_hook) };
			after_move_hook = nullptr;
			if (hook) hook(get_id());
		}
	};

	game_entity::entity create_entity(script::detail::script_creator creator, math::v3 position = {})
	{
		transform::init_info transform_info{};
		transform_info.position[0] = position.x;
		transform_info.position[1] = position.y;
		transform_info.position[2] = position.z;
		script::init_info script_info{ creator };

		game_entity::entity_info entity_info{};
		entity_info.transform = &transform_info;
		entity_info.script = creator ? &script_info : nullptr;
		return game_entity::create(entity_info);
	}

	size_t tag_of(const char* name)
	{
		return script::detail::string_hash()(name);
	}
}

// NOTE: REGISTER_SCRIPTは静的初期化時にregister_script()を呼び、結果を_reg_<TYPE>に保持する。
// USE_WITH_EDITOR(EngineUnitTestsとDebug/Release構成のエンジンでは未定義)の場合は
// 追加でadd_script_name()によりスクリプト名も記録されるが、登録処理自体は同じである。
REGISTER_SCRIPT(script_test_counter);
REGISTER_SCRIPT(script_test_mover);

DXFORGE_TEST_CASE(script_register_script_macro_registers_creator_by_tag)
{
	CHECK(_reg_script_test_counter == 1);
	CHECK(_reg_script_test_mover == 1);

	CHECK(script::detail::get_script_creator(tag_of("script_test_counter")) == &script::detail::create_script<script_test_counter>);
	CHECK(script::detail::get_script_creator(tag_of("script_test_mover")) == &script::detail::create_script<script_test_mover>);
}

// #113: 未登録のタグではassertせず、nullptrを返す
DXFORGE_TEST_CASE(script_get_script_creator_returns_null_for_unregistered_tag)
{
	CHECK(script::detail::get_script_creator(tag_of("script_test_not_registered")) == nullptr);
}

DXFORGE_TEST_CASE(script_entity_created_with_script_has_valid_script_component)
{
	const script_counters before{ counters };

	const game_entity::entity entity{ create_entity(script::detail::get_script_creator(tag_of("script_test_counter"))) };
	REQUIRE(entity.is_valid());
	REQUIRE(game_entity::is_alive(entity.get_id()));

	CHECK(entity.script().is_valid());
	CHECK(entity.transform().is_valid());
	// script_creatorはエンティティ生成時にちょうど1回呼ばれる
	CHECK(counters.created == before.created + 1);
	CHECK(counters.destroyed == before.destroyed);

	game_entity::remove(entity.get_id());
}

DXFORGE_TEST_CASE(script_entity_created_without_script_has_invalid_script_component)
{
	const script_counters before{ counters };

	const game_entity::entity entity{ create_entity(nullptr) };
	REQUIRE(entity.is_valid());
	REQUIRE(game_entity::is_alive(entity.get_id()));

	CHECK(!entity.script().is_valid());
	CHECK(counters.created == before.created);

	game_entity::remove(entity.get_id());
}

DXFORGE_TEST_CASE(script_entity_remove_destroys_script_instance)
{
	const script_counters before{ counters };

	const game_entity::entity entity{ create_entity(&script::detail::create_script<script_test_counter>) };
	REQUIRE(entity.is_valid());
	REQUIRE(entity.script().is_valid());

	game_entity::remove(entity.get_id());
	CHECK(!game_entity::is_alive(entity.get_id()));
	CHECK(counters.destroyed == before.destroyed + 1);

	// 削除済みスクリプトはupdateで呼ばれない
	script::update(1.f);
	CHECK(counters.updated == before.updated);
}

DXFORGE_TEST_CASE(script_update_calls_update_on_each_live_script_with_dt)
{
	const script_counters before{ counters };

	const game_entity::entity a{ create_entity(&script::detail::create_script<script_test_counter>) };
	const game_entity::entity b{ create_entity(&script::detail::create_script<script_test_counter>) };
	REQUIRE(a.is_valid() && b.is_valid());

	script::update(0.5f);
	CHECK(counters.updated == before.updated + 2);
	CHECK(counters.total_dt == before.total_dt + 1.f);

	// 片方を削除した後は、残ったスクリプトだけが更新される
	game_entity::remove(a.get_id());
	script::update(0.25f);
	CHECK(counters.updated == before.updated + 3);
	CHECK(counters.total_dt == before.total_dt + 1.25f);
	CHECK(counters.last_updated_id == b.get_id());

	game_entity::remove(b.get_id());
	CHECK(counters.destroyed == before.destroyed + 2);
}

DXFORGE_TEST_CASE(script_set_position_in_update_is_applied_to_transform)
{
	const game_entity::entity entity{ create_entity(&script::detail::create_script<script_test_mover>) };
	REQUIRE(entity.is_valid());
	REQUIRE(entity.script().is_valid());

	// set_position()はキャッシュに積まれ、script::update()の最後にtransform::update()で反映される
	script::update(0.5f);
	const math::v3 position{ entity.position() };
	CHECK(position.x == 0.5f);
	CHECK(position.y == 2.f);
	CHECK(position.z == 3.f);

	game_entity::remove(entity.get_id());
}

DXFORGE_TEST_CASE(script_components_stay_valid_across_id_reuse)
{
	// NOTE: スクリプトIDの再利用はfree_ids.size() > id::min_deleted_elementsで初めて発生するため、
	// それを超える数のスクリプト付きエンティティを生成・削除してから再生成する。
	constexpr u32 count{ id::min_deleted_elements + 16 };
	const script_counters before{ counters };

	utl::vector<game_entity::entity_id> ids{};
	for (u32 i{ 0 }; i < count; ++i)
	{
		const game_entity::entity entity{ create_entity(&script::detail::create_script<script_test_counter>) };
		REQUIRE(entity.is_valid());
		ids.emplace_back(entity.get_id());
	}
	for (const game_entity::entity_id entity_id : ids)
	{
		game_entity::remove(entity_id);
	}
	CHECK(counters.destroyed == before.destroyed + count);

	ids.clear();
	for (u32 i{ 0 }; i < count; ++i)
	{
		const game_entity::entity entity{ create_entity(&script::detail::create_script<script_test_counter>) };
		REQUIRE(entity.is_valid());
		CHECK(entity.script().is_valid());
		ids.emplace_back(entity.get_id());
	}

	script::update(1.f);
	CHECK(counters.updated == before.updated + count);

	for (const game_entity::entity_id entity_id : ids)
	{
		game_entity::remove(entity_id);
	}
	CHECK(counters.created == before.created + count * 2);
	CHECK(counters.destroyed == before.destroyed + count * 2);
}

DXFORGE_TEST_CASE(script_id_at_max_generation_is_not_reused)
{
	// NOTE: script::remove()は、ID再利用の可否をgenerations[id::index(id)]で判定しなければならない(#21)。
	// 以前は密なインデックス(=削除時点の生存スクリプト数)で引いていたため、
	// そのインデックスのスロットが低い世代のまま残っていると、上限に達したIDまで再利用されていた。
	// ここでは「生存スクリプト数 == 世代0のまま保持しているスロットのインデックス」という状態を作り、
	// その上で1スロットずつ生成・削除を繰り返して世代を上限まで進める。
	const auto creator{ &script::detail::create_script<script_test_counter> };

	struct held_script
	{
		game_entity::entity_id entity_id;
		id::id_type script_index;
	};
	utl::vector<held_script> held{};
	const auto hold_one = [&]() -> script::script_id
	{
		const game_entity::entity entity{ create_entity(creator) };
		const script::script_id script_id{ entity.script().get_id() };
		held.emplace_back(held_script{ entity.get_id(), id::index(script_id) });
		return script_id;
	};

	// 1. 解放済みIDのキューを使い切り、新規スロット(世代0)が割り当てられるまで保持し続ける。
	//    再利用されたIDは必ず世代1以上なので、世代0なら新規スロットである。
	//    このときキューの長さはちょうどid::min_deleted_elementsになっている。
	id::id_type pinned_index{ id::invalid_id };
	for (u32 i{ 0 }; i < 1'000'000; ++i)
	{
		const script::script_id script_id{ hold_one() };
		if (id::generation(script_id) == 0)
		{
			pinned_index = id::index(script_id);
			break;
		}
	}
	REQUIRE(id::is_valid(pinned_index));
	REQUIRE(pinned_index >= held.size());

	// 2. 新規スロットの割り当てを続け、保持数をpinned_index + 1にしてから最後の1つを削除する。
	//    これで生存スクリプト数 == pinned_index、解放済みIDのキューはid::min_deleted_elements + 1になる。
	while (held.size() < pinned_index + 1)
	{
		hold_one();
	}
	REQUIRE(held.back().script_index != pinned_index);
	game_entity::remove(held.back().entity_id);
	held.resize(held.size() - 1);
	REQUIRE(held.size() == pinned_index);

	// 3. 1つ生成して削除する操作を繰り返し、キュー内のスロットの世代を上限まで進める。
	//    修正前は削除時にgenerations[pinned_index](=0)を見るため上限のIDもキューに戻り、
	//    キューを一周した後で世代がid::max_generationを超えたIDが生成されていた。
	constexpr u32 queue_length{ id::min_deleted_elements + 1 };
	constexpr u32 max_cycles{ (id::max_generation + 4) * queue_length };
	bool reached_max_generation{ false };
	bool exceeded_max_generation{ false };
	u32 cycles_after_max{ 0 };
	for (u32 i{ 0 }; i < max_cycles && cycles_after_max < queue_length * 2; ++i)
	{
		const game_entity::entity entity{ create_entity(creator) };
		const id::id_type generation{ id::generation(entity.script().get_id()) };
		if (generation > id::max_generation) exceeded_max_generation = true;
		if (generation == id::max_generation) reached_max_generation = true;
		if (reached_max_generation) ++cycles_after_max;
		game_entity::remove(entity.get_id());
	}
	CHECK(reached_max_generation);
	CHECK(!exceeded_max_generation);

	for (const held_script& h : held)
	{
		game_entity::remove(h.entity_id);
	}
}

DXFORGE_TEST_CASE(script_begin_play_is_called_once_before_first_update)
{
	const script_counters before{ counters };

	const game_entity::entity a{ create_entity(&script::detail::create_script<script_test_counter>) };
	REQUIRE(a.is_valid() && a.script().is_valid());
	// NOTE: begin_play()は生成時ではなく、最初のscript::update()の冒頭で呼ばれる。
	// エディタはゲームDLLを読み込んだままエンティティを生成するため、生成時に呼ぶとエディタ内でゲームコードが動いてしまう。
	CHECK(counters.begun == before.begun);

	script::update(1.f);
	CHECK(counters.begun == before.begun + 1);
	CHECK(counters.updated == before.updated + 1);

	// 2回目以降のupdateではbegin_play()は呼ばれない
	script::update(1.f);
	script::update(1.f);
	CHECK(counters.begun == before.begun + 1);
	CHECK(counters.updated == before.updated + 3);

	// 途中で追加されたスクリプトは、次のupdateで自分のupdate()より前に1回だけbegin_play()が呼ばれる
	const game_entity::entity b{ create_entity(&script::detail::create_script<script_test_counter>) };
	REQUIRE(b.is_valid() && b.script().is_valid());
	script::update(1.f);
	script::update(1.f);
	CHECK(counters.begun == before.begun + 2);
	CHECK(counters.updated == before.updated + 7);
	CHECK(counters.updated_before_begin_play == before.updated_before_begin_play);

	game_entity::remove(a.get_id());
	game_entity::remove(b.get_id());
}

DXFORGE_TEST_CASE(script_removed_before_first_update_does_not_begin_play)
{
	const script_counters before{ counters };

	const game_entity::entity entity{ create_entity(&script::detail::create_script<script_test_counter>) };
	REQUIRE(entity.is_valid() && entity.script().is_valid());
	game_entity::remove(entity.get_id());

	script::update(1.f);
	CHECK(counters.begun == before.begun);
	CHECK(counters.updated == before.updated);
}

DXFORGE_TEST_CASE(script_removed_before_update_does_not_stay_in_begin_play_queue)
{
	// NOTE: エディタはscript::update()を呼ばずに、ゲームDLLを読み直すたびにスクリプトの削除と再生成を繰り返す。
	//		 削除したスクリプトが待ちリストに残ると、update()が呼ばれない限り待ちリストが増え続ける。
	const u32 pending_before{ script::detail::pending_begin_play_count() };

	for (u32 i{ 0 }; i < 100; ++i)
	{
		const game_entity::entity entity{ create_entity(&script::detail::create_script<script_test_counter>) };
		REQUIRE(entity.is_valid() && entity.script().is_valid());
		CHECK(script::detail::pending_begin_play_count() == pending_before + 1);
		game_entity::remove(entity.get_id());
	}
	CHECK(script::detail::pending_begin_play_count() == pending_before);
}

DXFORGE_TEST_CASE(script_removed_inside_begin_play_is_skipped)
{
	const script_counters before{ counters };

	// removerのbegin_play()が、同じフレームでまだbegin_play()を呼ばれていないtargetを削除する
	const game_entity::entity remover{ create_entity(&script::detail::create_script<script_test_remover>) };
	const game_entity::entity target{ create_entity(&script::detail::create_script<script_test_counter>) };
	const game_entity::entity survivor{ create_entity(&script::detail::create_script<script_test_counter>) };
	REQUIRE(remover.is_valid() && target.is_valid() && survivor.is_valid());
	entity_to_remove_in_begin_play = target.get_id();

	script::update(1.f);
	CHECK(!game_entity::is_alive(target.get_id()));
	CHECK(counters.destroyed == before.destroyed + 1);
	// targetは削除されたのでbegin_play()もupdate()も呼ばれず、survivorだけが呼ばれる
	CHECK(counters.begun == before.begun + 1);
	CHECK(counters.updated == before.updated + 1);
	CHECK(counters.updated_before_begin_play == before.updated_before_begin_play);
	CHECK(script::detail::pending_begin_play_count() == 0);

	game_entity::remove(remover.get_id());
	game_entity::remove(survivor.get_id());
}

DXFORGE_TEST_CASE(script_created_inside_update_begins_play_before_its_first_update)
{
	// NOTE: update()の中での生成は、entity_scriptsの再確保を起こす。修正前は範囲forのイテレータが
	//		 解放済みの領域を指したままになり、後ろのスクリプトのupdate()で未定義動作になっていた(#54)。
	//		 再確保を確実に起こすため、1回のupdate()で多数のエンティティを生成する。
	constexpr u32 spawn_count{ 4096 };
	const auto creator{ &script::detail::create_script<script_test_counter> };

	const game_entity::entity spawner{ create_entity(&script::detail::create_script<script_test_hook>) };
	const game_entity::entity survivor{ create_entity(creator) };
	REQUIRE(spawner.is_valid() && survivor.is_valid());

	// 1フレーム目でbegin_play()を済ませておく
	script::update(1.f);
	const script_counters before{ counters };
	const u32 pending_before{ script::detail::pending_begin_play_count() };

	utl::vector<game_entity::entity_id> spawned{};
	update_hook = [&](game_entity::entity_id)
	{
		for (u32 i{ 0 }; i < spawn_count; ++i)
		{
			const game_entity::entity entity{ create_entity(creator) };
			REQUIRE(entity.is_valid() && entity.script().is_valid());
			spawned.emplace_back(entity.get_id());
		}
	};

	script::update(1.f);
	REQUIRE(spawned.size() == spawn_count);
	// 生成されたスクリプトは、そのフレームではbegin_play()もupdate()も呼ばれず、次のupdate()を待つ(#47)
	CHECK(counters.begun == before.begun);
	CHECK(counters.updated == before.updated + 1);
	CHECK(update_counts[survivor.get_id()] == 2);
	CHECK(script::detail::pending_begin_play_count() == pending_before + spawn_count);
	u32 spawned_updated{ 0 };
	for (const game_entity::entity_id id : spawned) spawned_updated += update_counts[id];
	CHECK(spawned_updated == 0);

	// 次のupdate()で、生成された分のbegin_play()が自分のupdate()より前に1回だけ呼ばれる
	script::update(1.f);
	CHECK(counters.begun == before.begun + spawn_count);
	CHECK(counters.updated == before.updated + 1 + spawn_count + 1);
	CHECK(counters.updated_before_begin_play == before.updated_before_begin_play);
	CHECK(script::detail::pending_begin_play_count() == pending_before);
	spawned_updated = 0;
	for (const game_entity::entity_id id : spawned) spawned_updated += update_counts[id];
	CHECK(spawned_updated == spawn_count);

	update_hook = nullptr;
	for (const game_entity::entity_id id : spawned) game_entity::remove(id);
	game_entity::remove(spawner.get_id());
	game_entity::remove(survivor.get_id());
}

DXFORGE_TEST_CASE(script_removed_inside_update_is_not_updated_and_others_update_once)
{
	// NOTE: 修正前はremove()がupdate()のループ中にerase_unordered()で要素を入れ替えていたため、
	//		 末尾から移されたスクリプトが飛ばされ、自分自身を削除したスクリプトは実行中に破棄されていた(#54)。
	const auto creator{ &script::detail::create_script<script_test_counter> };

	const game_entity::entity a{ create_entity(creator) };
	const game_entity::entity killer{ create_entity(&script::detail::create_script<script_test_hook>) };
	const game_entity::entity b{ create_entity(creator) };
	const game_entity::entity c{ create_entity(creator) };
	const game_entity::entity d{ create_entity(creator) };
	REQUIRE(a.is_valid() && killer.is_valid() && b.is_valid() && c.is_valid() && d.is_valid());

	script::update(1.f);
	const script_counters before{ counters };
	const u32 a_before{ update_counts[a.get_id()] };
	const u32 b_before{ update_counts[b.get_id()] };
	const u32 c_before{ update_counts[c.get_id()] };
	const u32 d_before{ update_counts[d.get_id()] };

	// killerは、すでに更新されたかもしれないa、まだ更新されていないかもしれないc、自分自身を削除する
	update_hook = [&](game_entity::entity_id self)
	{
		game_entity::remove(a.get_id());
		game_entity::remove(c.get_id());
		game_entity::remove(self);
	};

	script::update(1.f);
	CHECK(!game_entity::is_alive(a.get_id()));
	CHECK(!game_entity::is_alive(c.get_id()));
	CHECK(!game_entity::is_alive(killer.get_id()));
	// 削除されたスクリプトはupdate()が返るまでに破棄される
	CHECK(counters.destroyed == before.destroyed + 2);
	// 削除されずに残ったスクリプトは、ちょうど1回ずつ更新される
	CHECK(update_counts[b.get_id()] == b_before + 1);
	CHECK(update_counts[d.get_id()] == d_before + 1);
	CHECK(update_counts[a.get_id()] <= a_before + 1);
	CHECK(update_counts[c.get_id()] <= c_before + 1);

	// 次のフレームでは、残ったbとdだけが更新される
	const u32 updated_mid{ counters.updated };
	script::update(1.f);
	CHECK(counters.updated == updated_mid + 2);
	CHECK(update_counts[b.get_id()] == b_before + 2);
	CHECK(update_counts[d.get_id()] == d_before + 2);

	update_hook = nullptr;
	game_entity::remove(b.get_id());
	game_entity::remove(d.get_id());
}

DXFORGE_TEST_CASE(script_created_and_removed_inside_same_update_is_never_called)
{
	const script_counters before{ counters };

	const game_entity::entity actor{ create_entity(&script::detail::create_script<script_test_hook>) };
	REQUIRE(actor.is_valid());
	script::update(1.f);

	update_hook = [&](game_entity::entity_id)
	{
		const game_entity::entity temp{ create_entity(&script::detail::create_script<script_test_counter>) };
		REQUIRE(temp.is_valid() && temp.script().is_valid());
		game_entity::remove(temp.get_id());
	};

	script::update(1.f);
	CHECK(counters.created == before.created + 1);
	CHECK(counters.destroyed == before.destroyed + 1);
	CHECK(counters.begun == before.begun);
	CHECK(counters.updated == before.updated);
	CHECK(script::detail::pending_begin_play_count() == 0);

	script::update(1.f);
	CHECK(counters.begun == before.begun);
	CHECK(counters.updated == before.updated);

	update_hook = nullptr;
	game_entity::remove(actor.get_id());
}

DXFORGE_TEST_CASE(script_removing_itself_inside_begin_play_is_safe)
{
	const script_counters before{ counters };

	const game_entity::entity self_remover{ create_entity(&script::detail::create_script<script_test_hook>) };
	const game_entity::entity survivor{ create_entity(&script::detail::create_script<script_test_counter>) };
	REQUIRE(self_remover.is_valid() && survivor.is_valid());

	begin_play_hook = [&](game_entity::entity_id self)
	{
		game_entity::remove(self);
	};

	script::update(1.f);
	CHECK(!game_entity::is_alive(self_remover.get_id()));
	CHECK(counters.begun == before.begun + 1);
	CHECK(counters.updated == before.updated + 1);
	CHECK(counters.updated_before_begin_play == before.updated_before_begin_play);

	begin_play_hook = nullptr;
	game_entity::remove(survivor.get_id());
}

DXFORGE_TEST_CASE(script_removed_by_another_script_can_read_transform_in_destructor)
{
	// NOTE: update()/begin_play()の中で別のスクリプトに削除されたスクリプトは、エンティティ(Transform)が
	//		 無効になる前、つまりgame_entity::remove()の中で破棄されなければならない。
	//		 破棄をupdate()の最後まで遅らせると、デストラクタからposition()などを読めなくなる(#54のレビュー指摘)。
	const auto reader_creator{ &script::detail::create_script<script_test_reader> };
	const reader_result before{ reader };

	const game_entity::entity actor{ create_entity(&script::detail::create_script<script_test_hook>) };
	const game_entity::entity removed_in_begin_play{ create_entity(reader_creator) };
	REQUIRE(actor.is_valid() && removed_in_begin_play.is_valid());

	// 1. actorのbegin_play()の中で削除する
	begin_play_hook = [&](game_entity::entity_id)
	{
		game_entity::remove(removed_in_begin_play.get_id());
	};
	script::update(1.f);
	CHECK(!game_entity::is_alive(removed_in_begin_play.get_id()));
	CHECK(reader.destroyed == before.destroyed + 1);
	CHECK(reader.alive_in_destructor == before.alive_in_destructor + 1);

	// 2. actorのupdate()の中で削除する
	const game_entity::entity removed_in_update{ create_entity(reader_creator) };
	REQUIRE(removed_in_update.is_valid());
	script::update(1.f);
	update_hook = [&](game_entity::entity_id)
	{
		game_entity::remove(removed_in_update.get_id());
	};
	script::update(1.f);
	CHECK(!game_entity::is_alive(removed_in_update.get_id()));
	CHECK(reader.destroyed == before.destroyed + 2);
	CHECK(reader.alive_in_destructor == before.alive_in_destructor + 2);
	CHECK(reader.position_in_destructor.x == 0.f);
	CHECK(reader.position_in_destructor.y == 0.f);
	CHECK(reader.position_in_destructor.z == 0.f);

	begin_play_hook = nullptr;
	update_hook = nullptr;
	game_entity::remove(actor.get_id());
}

DXFORGE_TEST_CASE(script_entity_removed_after_set_position_in_update_is_not_written)
{
	// NOTE: update()の中でset_position()を呼んだエンティティが同じフレームで削除されると、
	//		 script::update()の最後に適用するTransformのキャッシュが、削除済みのスロットに書き込んでいた(#78)。
	//		 Releaseでは死んだスロットの中身は外から見えないため、Debugではtransform::update()のassertで検出する。
	const game_entity::entity mover{ create_entity(&script::detail::create_script<script_test_move_then_hook>) };
	REQUIRE(mover.is_valid() && mover.script().is_valid());

	after_move_hook = [](game_entity::entity_id self)
	{
		game_entity::remove(self);
	};
	script::update(1.f);
	CHECK(!game_entity::is_alive(mover.get_id()));

	// 次のフレームに古いキャッシュが残っていないこと
	script::update(1.f);

	after_move_hook = nullptr;
}

DXFORGE_TEST_CASE(script_stale_transform_cache_does_not_overwrite_entity_reusing_id)
{
	// NOTE: エンティティIDの再利用はfree_ids.size() > id::min_deleted_elementsで初めて発生し、free_idsは先入れ先出しである。
	//		 そのため、update()の中で「set_position()したエンティティを削除し、その後ろに1025個以上削除してから、
	//		 削除したエンティティのインデックスが再利用されるまで生成する」と、同じフレームの中で同じインデックス・別の世代の
	//		 エンティティができる。修正前は、削除した古いエンティティ用のキャッシュが新しいエンティティのTransformを上書きしていた(#78)。
	const math::v3 new_position{ 1.f, 2.f, 3.f };
	constexpr u32 helper_count{ id::min_deleted_elements + 1 };
	constexpr u32 max_creations{ id::min_deleted_elements * 4 };

	utl::vector<game_entity::entity_id> helpers{};
	for (u32 i{ 0 }; i < helper_count; ++i)
	{
		const game_entity::entity helper{ create_entity(nullptr) };
		REQUIRE(helper.is_valid());
		helpers.emplace_back(helper.get_id());
	}

	const game_entity::entity mover{ create_entity(&script::detail::create_script<script_test_move_then_hook>) };
	REQUIRE(mover.is_valid() && mover.script().is_valid());

	utl::vector<game_entity::entity_id> created{};
	game_entity::entity_id reused{ id::invalid_id };
	after_move_hook = [&](game_entity::entity_id self)
	{
		const id::id_type stale_index{ id::index(self) };
		game_entity::remove(self);
		for (const game_entity::entity_id helper : helpers)
		{
			game_entity::remove(helper);
		}

		for (u32 i{ 0 }; i < max_creations; ++i)
		{
			const game_entity::entity entity{ create_entity(nullptr, new_position) };
			created.emplace_back(entity.get_id());
			if (id::index(entity.get_id()) == stale_index)
			{
				reused = entity.get_id();
				break;
			}
		}
	};
	script::update(1.f);

	CHECK(id::is_valid(reused));
	if (id::is_valid(reused))
	{
		CHECK(reused != mover.get_id());
		CHECK(game_entity::is_alive(reused));
		const math::v3 position{ game_entity::entity{ reused }.position() };
		CHECK(position.x == new_position.x);
		CHECK(position.y == new_position.y);
		CHECK(position.z == new_position.z);
	}

	after_move_hook = nullptr;
	for (const game_entity::entity_id entity_id : created)
	{
		game_entity::remove(entity_id);
	}
}

// #146: エディタのGetScriptNames()に渡すスクリプト名のSAFEARRAYを、ATLを使わずに作る。
// get_script_names()(USE_WITH_EDITORのときだけ公開)は、この関数にscript_names()を渡すだけである。
// エディタは名前をGetScriptCreater(string)にANSI(CP_ACP)でマーシャリングして渡し、DLLはそのバイト列のハッシュで探すので、
// 名前はCP_ACPで復号しなければならない(登録時のバイト列に戻らないと、スクリプトが見つからなくなる)。
// ACPは環境によって違う(CIは1252、日本語のWindowsは932)ので、テストの名前はACPで往復できる文字を選んで作る。
namespace
{
	/// REQUIRE で中断してもSAFEARRAYを解放するためのガード
	struct safe_array_guard
	{
		SAFEARRAY* array{ nullptr };
		HRESULT destroy()
		{
			const HRESULT hr{ array ? SafeArrayDestroy(array) : S_OK };
			array = nullptr;
			return hr;
		}
		~safe_array_guard() { destroy(); }
	};

	/// 1次元・下限0のBSTRのSAFEARRAYを、wstringの配列として読む
	std::vector<std::wstring> read_bstr_array(SAFEARRAY* array)
	{
		std::vector<std::wstring> result;
		LONG lower{ -1 };
		LONG upper{ -2 };
		REQUIRE(SUCCEEDED(SafeArrayGetLBound(array, 1, &lower)));
		REQUIRE(SUCCEEDED(SafeArrayGetUBound(array, 1, &upper)));
		REQUIRE(lower == 0);

		BSTR* data{ nullptr };
		REQUIRE(SUCCEEDED(SafeArrayAccessData(array, (void**)&data)));
		for (LONG i{ lower }; i <= upper; ++i)
		{
			const BSTR bstr{ data[i] };
			result.emplace_back(bstr ? std::wstring{ bstr, SysStringLen(bstr) } : std::wstring{});
		}
		REQUIRE(SUCCEEDED(SafeArrayUnaccessData(array)));
		return result;
	}

	/// コードページがUTF-8(65001)か。CP_ACPのときは実際のACPで判断する
	bool is_utf8_code_page(UINT code_page)
	{
		return code_page == CP_UTF8 || (code_page == CP_ACP && GetACP() == CP_UTF8);
	}

	/// UTF-16をコードページのバイト列にする(エディタのANSIマーシャリングは CP_ACP)
	/// @param lossless 置き換え文字(既定の文字・最も近い文字)なしで変換できたらtrue
	std::string to_code_page(UINT code_page, const std::wstring& text, bool* lossless = nullptr)
	{
		if (lossless) *lossless = false;
		if (text.empty())
		{
			if (lossless) *lossless = true;
			return {};
		}
		// NOTE: UTF-8のコードページには、WC_NO_BEST_FIT_CHARS と lpUsedDefaultChar を渡せない
		const bool utf8{ is_utf8_code_page(code_page) };
		const DWORD flags{ utf8 ? 0u : (DWORD)WC_NO_BEST_FIT_CHARS };
		BOOL used_default{ FALSE };
		BOOL* const used_default_ptr{ utf8 ? nullptr : &used_default };
		const int length{ WideCharToMultiByte(code_page, flags, text.data(), (int)text.size(), nullptr, 0, nullptr, used_default_ptr) };
		if (length <= 0) return {};
		std::string result((size_t)length, '\0');
		if (WideCharToMultiByte(code_page, flags, text.data(), (int)text.size(), result.data(), length, nullptr, used_default_ptr) != length) return {};
		if (lossless) *lossless = !used_default;
		return result;
	}

	std::string to_acp(const std::wstring& text)
	{
		return to_code_page(CP_ACP, text);
	}

	/// コードページのバイト列をUTF-16にする。そのコードページとして不正なら空
	std::wstring from_code_page(UINT code_page, const std::string& bytes)
	{
		if (bytes.empty()) return {};
		const int length{ MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), nullptr, 0) };
		if (length <= 0) return {};
		std::wstring result((size_t)length, L'\0');
		if (MultiByteToWideChar(code_page, MB_ERR_INVALID_CHARS, bytes.data(), (int)bytes.size(), result.data(), length) != length) return {};
		return result;
	}

	/// コードページで往復でき(バイト列 → UTF-16 → 同じバイト列)、名前に使える文字(制御文字・置換文字でない)だけか
	bool round_trips(UINT code_page, const std::string& bytes)
	{
		const std::wstring wide{ from_code_page(code_page, bytes) };
		if (wide.empty()) return false;
		for (const wchar_t c : wide)
		{
			if (c < 0x20 || (c >= 0x7F && c <= 0x9F) || c == 0xFFFD) return false;
		}
		bool lossless{ false };
		return to_code_page(code_page, wide, &lossless) == bytes && lossless;
	}

	// NOTE: CHECK は式を狭い文字列リテラルにするので、ASCII以外の文字を含むリテラルは CHECK の中に直接書かない(C4566)
	constexpr const wchar_t* latin_candidate{ L"café" };			///< 1252などで往復できる
	constexpr const wchar_t* japanese_candidate{ L"スクリプト" };	///< 932で往復できる

	/// テストの名前に入れる、ASCII以外の文字
	struct non_ascii_text
	{
		std::string bytes;		///< コードページのバイト列
		std::wstring wide;		///< UTF-16
	};

	/// コードページで往復できる、ASCII以外の文字を探す
	/// @param valid_as_utf8 trueなら、UTF-8としても正しい2バイト(C2-DF + 80-BF)に限る
	///		   (UTF-8を先に試す復号では、このような名前をUTF-8として誤って復号してしまう)
	/// @return 見つからない場合、またはUTF-8のコードページ(ANSIとUTF-8の区別がない)では空
	non_ascii_text find_non_ascii_text(UINT code_page, bool valid_as_utf8)
	{
		if (is_utf8_code_page(code_page)) return {};

		// まず、これまで確かめてきた文字を試す(1252の「é」、932の「スクリプト」、1252の「Ã©」/932の半角カナ2文字になるC3 A9)
		if (valid_as_utf8)
		{
			const std::string bytes{ "\xC3\xA9" };
			if (round_trips(code_page, bytes)) return { bytes, from_code_page(code_page, bytes) };
		}
		else
		{
			const wchar_t* const candidates[]{ latin_candidate, japanese_candidate };
			for (const wchar_t* candidate : candidates)
			{
				bool lossless{ false };
				const std::string bytes{ to_code_page(code_page, candidate, &lossless) };
				if (lossless && round_trips(code_page, bytes) && from_code_page(code_page, bytes) == candidate) return { bytes, candidate };
			}
		}

		// ほかのコードページ(1251など)では、往復できるバイト列を探す
		std::vector<std::string> sequences;
		if (valid_as_utf8)
		{
			for (int lead{ 0xC2 }; lead <= 0xDF; ++lead)
				for (int trail{ 0x80 }; trail <= 0xBF; ++trail)
					sequences.push_back({ (char)lead, (char)trail });
		}
		else
		{
			for (int lead{ 0x80 }; lead <= 0xFF; ++lead)
			{
				if (!IsDBCSLeadByteEx(code_page, (BYTE)lead))
				{
					sequences.push_back({ (char)lead });
					continue;
				}
				for (int trail{ 0x40 }; trail <= 0xFE; ++trail)
					sequences.push_back({ (char)lead, (char)trail });
			}
		}
		for (const std::string& bytes : sequences)
		{
			if (round_trips(code_page, bytes)) return { bytes, from_code_page(code_page, bytes) };
		}
		return {};
	}
}

// テストの名前を選ぶ処理そのもののテスト。ACPが1252・932以外(1251など)でも、ASCII以外の名前を作れること
DXFORGE_TEST_CASE(script_test_finds_non_ascii_names_for_ansi_code_pages)
{
	const UINT code_pages[]{ 874, 932, 936, 949, 950, 1250, 1251, 1252, 1253, 1254, 1255, 1256, 1257, 1258 };
	for (const UINT code_page : code_pages)
	{
		if (!IsValidCodePage(code_page)) continue;
		for (const bool valid_as_utf8 : { false, true })
		{
			const non_ascii_text text{ find_non_ascii_text(code_page, valid_as_utf8) };
			if (text.bytes.empty()) std::printf("    no non-ASCII name for code page %u (valid_as_utf8=%d)\n", code_page, valid_as_utf8 ? 1 : 0);
			CHECK(!text.bytes.empty());
			CHECK(round_trips(code_page, text.bytes));
			CHECK(std::any_of(text.wide.begin(), text.wide.end(), [](wchar_t c) { return c > 0x7F; }));
		}
	}
	// 1252と932では、これまでと同じ文字を使う
	if (IsValidCodePage(1252))
	{
		CHECK(find_non_ascii_text(1252, false).wide == latin_candidate);
		CHECK(find_non_ascii_text(1252, true).bytes == "\xC3\xA9");
	}
	if (IsValidCodePage(932))
	{
		CHECK(find_non_ascii_text(932, false).wide == japanese_candidate);
		CHECK(find_non_ascii_text(932, true).bytes == "\xC3\xA9");
	}
	// UTF-8のコードページでは、ANSIとUTF-8の区別がないので探さない
	CHECK(find_non_ascii_text(CP_UTF8, false).bytes.empty());
}

DXFORGE_TEST_CASE(script_names_safe_array_has_bstr_elements_matching_names)
{
	std::vector<std::string> names{ "script_test_counter", "" };
	std::vector<std::wstring> expected{ L"script_test_counter", L"" };

	const non_ascii_text non_ascii{ find_non_ascii_text(CP_ACP, false) };
	if (non_ascii.bytes.empty())
	{
		std::printf("    [SKIP] non-ASCII name: no character round-trips through the ANSI code page %u\n", GetACP());
	}
	else
	{
		names.push_back("script_test_acp_" + non_ascii.bytes);
		expected.push_back(L"script_test_acp_" + non_ascii.wide);
	}
	const u32 count{ (u32)names.size() };

	safe_array_guard guard{ script::detail::make_script_names_safe_array(names.data(), count) };
	REQUIRE(guard.array != nullptr);
	CHECK(SafeArrayGetDim(guard.array) == 1);
	VARTYPE vt{ VT_EMPTY };
	CHECK(SUCCEEDED(SafeArrayGetVartype(guard.array, &vt)));
	CHECK(vt == VT_BSTR);

	const std::vector<std::wstring> actual{ read_bstr_array(guard.array) };
	REQUIRE(actual.size() == count);
	for (u32 i{ 0 }; i < count; ++i)
	{
		CHECK(actual[i] == expected[i]);
	}
	// 呼び出し側(エディタのマーシャラー)と同じく SafeArrayDestroy で解放できる
	CHECK(guard.destroy() == S_OK);
}

DXFORGE_TEST_CASE(script_names_safe_array_is_null_for_no_names)
{
	const std::string names[]{ "script_test_counter" };
	CHECK(script::detail::make_script_names_safe_array(names, 0) == nullptr);
	CHECK(script::detail::make_script_names_safe_array(nullptr, 0) == nullptr);
	CHECK(script::detail::make_script_names_safe_array(nullptr, 1) == nullptr);
}

// エディタと同じ往復(登録した名前 → get_script_names()のBSTR → ANSIマーシャリング → ハッシュ → get_script_creator)で、
// 登録したスクリプトが見つかる。ATLのA2BSTR_EXもACPで復号していたので、往復でバイト列が保たれていた
DXFORGE_TEST_CASE(script_names_safe_array_round_trips_through_ansi_marshaling_to_script_creator)
{
	std::vector<std::string> names{ "script_test_acp_ascii" };
	const non_ascii_text non_ascii{ find_non_ascii_text(CP_ACP, false) };
	const non_ascii_text valid_as_utf8{ find_non_ascii_text(CP_ACP, true) };
	if (non_ascii.bytes.empty() || valid_as_utf8.bytes.empty())
	{
		std::printf("    [SKIP] non-ASCII names: no character round-trips through the ANSI code page %u\n", GetACP());
	}
	if (!non_ascii.bytes.empty()) names.push_back("script_test_acp_" + non_ascii.bytes);
	if (!valid_as_utf8.bytes.empty()) names.push_back("script_test_acp_utf8_" + valid_as_utf8.bytes);
	const u32 count{ (u32)names.size() };

	// REGISTER_SCRIPT と同じく、名前のバイト列のハッシュで登録する
	const script::detail::script_creator creator{ &script::detail::create_script<script_test_counter> };
	for (const std::string& name : names)
	{
		REQUIRE(script::detail::register_script(script::detail::string_hash()(name), creator) == 1);
	}

	safe_array_guard guard{ script::detail::make_script_names_safe_array(names.data(), count) };
	REQUIRE(guard.array != nullptr);
	const std::vector<std::wstring> actual{ read_bstr_array(guard.array) };
	REQUIRE(actual.size() == count);
	for (u32 i{ 0 }; i < count; ++i)
	{
		const std::string marshaled{ to_acp(actual[i]) };
		CHECK(marshaled == names[i]);
		CHECK(script::detail::get_script_creator(script::detail::string_hash()(marshaled)) == creator);
	}
}
