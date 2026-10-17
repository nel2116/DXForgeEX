// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_Script.cpp]
// 作成日 : 2026/10/18
// 作成者 : 田中ミノル
// 概要 :
// Components/Script.cppのスクリプト登録・生成・削除・更新ロジックのテスト
// 更新履歴
// 2026/10/18 新規作成
// 2026/10/18 世代が上限に達したスクリプトIDを再利用しないことのテストを追加 (#21)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"
#include "CommonHeaders.h"
#include "Components/Entity.h"
#include "Components/Transform.h"
#include "Components/Script.h"

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
		f32 total_dt{ 0.f };
		game_entity::entity_id last_updated_id{ id::invalid_id };
	};

	script_counters counters{};

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

		void update(f32 dt) override
		{
			++counters.updated;
			counters.total_dt += dt;
			counters.last_updated_id = get_id();
		}
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

	game_entity::entity create_entity(script::detail::script_creator creator)
	{
		transform::init_info transform_info{};
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
