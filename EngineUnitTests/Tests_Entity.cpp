// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_Entity.cpp]
// 作成日 : 2026/10/18
// 作成者 : 田中ミノル
// 概要 :
// Components/Entity.cppとComponents/Transform.cppの生成・削除・世代管理のテスト
// 更新履歴
// 2026/10/18 新規作成
// 2026/10/18 orientation()の廃止に伴いローカルフレーム(right/up/front)の検証に変更 (#4)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"
#include "CommonHeaders.h"
#include "Components/Entity.h"
#include "Components/Transform.h"

#include <cmath>
#include <vector>

using namespace dxforge;

// NOTE: エンティティとTransformの状態はEntity.cpp/Transform.cppの匿名名前空間にある
//		プロセス全体の静的データであり、ランナー内の他のテストケースと共有される。
//		そのため各テストは自分が生成したエンティティを必ずすべて削除し、
//		特定のインデックス値や空のワールドを前提にしない。
//		また、エンジン側は誤用時にassertする(Debugではランナーごと落ちる)ため、
//		is_alive()は生成済みのIDに対してのみ、remove()は生存中のIDに対してのみ呼ぶ。

namespace
{
	// テスト用のTransform初期化情報を作成する(回転は単位クォータニオン)
	transform::init_info make_transform_info(f32 x, f32 y, f32 z)
	{
		transform::init_info info{};
		info.position[0] = x;
		info.position[1] = y;
		info.position[2] = z;
		info.rotation[3] = 1.f;
		return info;
	}

	game_entity::entity create_entity(transform::init_info& transform_info)
	{
		game_entity::entity_info entity_info{};
		entity_info.transform = &transform_info;
		return game_entity::create(entity_info);
	}

	bool ids_equal(id::id_type a, id::id_type b)
	{
		return a == b;
	}

	bool nearly_equal(f32 a, f32 b)
	{
		return std::fabs(a - b) < 1e-5f;
	}

	// 固定シードの線形合同法。rand()のグローバル状態やCRT実装に依存しないようにする。
	struct lcg
	{
		u32 state;
		u32 next()
		{
			state = state * 1664525u + 1013904223u;
			return state >> 8;
		}
	};
}

DXFORGE_TEST_CASE(entity_create_returns_alive_entity_and_remove_kills_it)
{
	transform::init_info transform_info{ make_transform_info(0.f, 0.f, 0.f) };
	const game_entity::entity entity{ create_entity(transform_info) };
	REQUIRE(entity.is_valid());
	REQUIRE(id::is_valid(entity.get_id()));
	CHECK(game_entity::is_alive(entity.get_id()));

	game_entity::remove(entity.get_id());
	CHECK(!game_entity::is_alive(entity.get_id()));
}

DXFORGE_TEST_CASE(entity_is_alive_tracks_each_entity_independently)
{
	constexpr u32 count{ 64 };
	transform::init_info transform_info{ make_transform_info(0.f, 0.f, 0.f) };
	std::vector<game_entity::entity> entities;
	for (u32 i{ 0 }; i < count; ++i)
	{
		entities.push_back(create_entity(transform_info));
	}

	bool all_valid{ true };
	for (const game_entity::entity& entity : entities)
	{
		all_valid = all_valid && entity.is_valid();
	}
	if (!all_valid)
	{
		// 有効なものだけ片付けてから中断する
		for (const game_entity::entity& entity : entities)
		{
			if (entity.is_valid()) game_entity::remove(entity.get_id());
		}
		REQUIRE(all_valid);
	}

	// 同時に生存しているエンティティのIDとインデックスは重複しない
	for (u32 i{ 0 }; i < count; ++i)
	{
		CHECK(game_entity::is_alive(entities[i].get_id()));
		for (u32 j{ i + 1 }; j < count; ++j)
		{
			CHECK(id::index(entities[i].get_id()) != id::index(entities[j].get_id()));
		}
	}

	// 偶数番目だけを削除し、奇数番目が巻き込まれないことを確認する
	for (u32 i{ 0 }; i < count; i += 2)
	{
		game_entity::remove(entities[i].get_id());
	}
	for (u32 i{ 0 }; i < count; ++i)
	{
		const bool expected_alive{ (i % 2) == 1 };
		CHECK(game_entity::is_alive(entities[i].get_id()) == expected_alive);
	}

	for (u32 i{ 1 }; i < count; i += 2)
	{
		game_entity::remove(entities[i].get_id());
		CHECK(!game_entity::is_alive(entities[i].get_id()));
	}
}

DXFORGE_TEST_CASE(entity_transform_is_created_with_entity_from_init_info)
{
	transform::init_info transform_info{};
	transform_info.position[0] = 1.f;
	transform_info.position[1] = -2.f;
	transform_info.position[2] = 3.5f;
	// Y軸まわりに90度回転するクォータニオン(x, y, z, w)
	const f32 half_sqrt2{ 0.70710678f };
	transform_info.rotation[1] = half_sqrt2;
	transform_info.rotation[3] = half_sqrt2;
	transform_info.scale[0] = 2.f;
	transform_info.scale[1] = 3.f;
	transform_info.scale[2] = 4.f;

	const game_entity::entity entity{ create_entity(transform_info) };
	REQUIRE(entity.is_valid());
	REQUIRE(game_entity::is_alive(entity.get_id()));

	// NOTE: TransformコンポーネントのIDはエンティティのIDと完全に一致する
	const transform::component transform{ entity.transform() };
	CHECK(transform.is_valid());
	CHECK(ids_equal(transform.get_id(), entity.get_id()));

	const math::v3 position{ entity.position() };
	CHECK(position.x == 1.f);
	CHECK(position.y == -2.f);
	CHECK(position.z == 3.5f);

	const math::v4 rotation{ entity.rotation() };
	CHECK(rotation.x == 0.f);
	CHECK(rotation.y == half_sqrt2);
	CHECK(rotation.z == 0.f);
	CHECK(rotation.w == half_sqrt2);

	const math::v3 scale{ entity.scale() };
	CHECK(scale.x == 2.f);
	CHECK(scale.y == 3.f);
	CHECK(scale.z == 4.f);

	// ローカルフレームは回転から導出される。Y軸まわり+90度(右手系)の回転では
	//	右(1, 0, 0) -> (0, 0, -1)、上(0, 1, 0) -> (0, 1, 0)のまま。
	//	前方はcross(right, up) = cross((0, 0, -1), (0, 1, 0)) = (1, 0, 0)で、
	//	(0, 0, 1)を同じ回転で回した結果とも一致する。
	const math::v3 right{ entity.right() };
	CHECK(nearly_equal(right.x, 0.f));
	CHECK(nearly_equal(right.y, 0.f));
	CHECK(nearly_equal(right.z, -1.f));

	const math::v3 up{ entity.up() };
	CHECK(nearly_equal(up.x, 0.f));
	CHECK(nearly_equal(up.y, 1.f));
	CHECK(nearly_equal(up.z, 0.f));

	const math::v3 front{ entity.front() };
	CHECK(nearly_equal(front.x, 1.f));
	CHECK(nearly_equal(front.y, 0.f));
	CHECK(nearly_equal(front.z, 0.f));

	// local_frame()は右・上・前方を行として並べた行列
	const math::m3x3 frame{ entity.local_frame() };
	CHECK(nearly_equal(frame._11, right.x) && nearly_equal(frame._12, right.y) && nearly_equal(frame._13, right.z));
	CHECK(nearly_equal(frame._21, up.x) && nearly_equal(frame._22, up.y) && nearly_equal(frame._23, up.z));
	CHECK(nearly_equal(frame._31, front.x) && nearly_equal(frame._32, front.y) && nearly_equal(frame._33, front.z));

	// NOTE: is_alive()はTransformコンポーネントの有効性も見ているため、
	//		削除後にfalseになることがTransformが無効化されたことの観測になる
	//		(削除後のentity.transform()はassertするので呼べない)。
	game_entity::remove(entity.get_id());
	CHECK(!game_entity::is_alive(entity.get_id()));
}

DXFORGE_TEST_CASE(entity_index_reuse_bumps_generation_and_keeps_old_id_dead)
{
	transform::init_info old_info{ make_transform_info(10.f, 20.f, 30.f) };
	const game_entity::entity old_entity{ create_entity(old_info) };
	REQUIRE(old_entity.is_valid());
	const game_entity::entity_id old_id{ old_entity.get_id() };
	game_entity::remove(old_id);
	CHECK(!game_entity::is_alive(old_id));

	// 削除済みIDはfree_ids(FIFO)の末尾に積まれ、free_idsがid::min_deleted_elementsを
	// 超えている間だけ先頭から再利用される。生成→削除を繰り返して、old_idの
	// インデックスが再利用されるまで回す。各インデックスはfree_idsに高々1回しか
	// 入らないので、同じインデックスが返ってきたらそれがold_idの再利用である。
	// 上限は他テストの残したfree_idsを考慮しても十分な値。
	transform::init_info new_info{ make_transform_info(-1.f, -2.f, -3.f) };
	constexpr u32 max_iterations{ 1000000 };
	game_entity::entity reused{};
	u32 churned{ 0 };
	for (u32 i{ 0 }; i < max_iterations && !reused.is_valid(); ++i)
	{
		const game_entity::entity entity{ create_entity(new_info) };
		if (!entity.is_valid()) break;
		if (id::index(entity.get_id()) == id::index(old_id))
		{
			reused = entity;
		}
		else
		{
			game_entity::remove(entity.get_id());
			++churned;
		}
	}
	REQUIRE(reused.is_valid());
	// 再利用には最低でもmin_deleted_elements回を超える削除が必要
	CHECK(churned >= id::min_deleted_elements);

	const game_entity::entity_id new_id{ reused.get_id() };
	CHECK(!ids_equal(new_id, old_id));
	CHECK(id::index(new_id) == id::index(old_id));
	CHECK(id::generation(new_id) == id::generation(old_id) + 1);

	// 同じインデックスでもTransformは生存しているので、old_idが死んでいるのは世代の不一致による
	CHECK(game_entity::is_alive(new_id));
	CHECK(!game_entity::is_alive(old_id));

	// 再利用スロットのTransformは新しいinit_infoで初期化し直されている
	const transform::component transform{ reused.transform() };
	CHECK(transform.is_valid());
	CHECK(ids_equal(transform.get_id(), new_id));
	const math::v3 position{ reused.position() };
	CHECK(position.x == -1.f);
	CHECK(position.y == -2.f);
	CHECK(position.z == -3.f);

	game_entity::remove(new_id);
	CHECK(!game_entity::is_alive(new_id));
	CHECK(!game_entity::is_alive(old_id));
}

// EngineTest/TestEntityComponent.hのランダム生成・削除テストを、固定シードかつ
// 有限回数で自動実行できるようにしたもの
DXFORGE_TEST_CASE(entity_random_create_remove_stress_keeps_state_consistent)
{
	constexpr u32 iterations{ 2000 };
	constexpr u32 min_population{ 1000 };
	lcg random{ 12345u };

	struct tracked_entity
	{
		game_entity::entity entity;
		f32 tag;	// position.xに埋め込んだ識別値
	};
	std::vector<tracked_entity> entities;
	u32 added{ 0 };
	u32 removed{ 0 };
	u32 failures{ 0 };

	for (u32 iteration{ 0 }; iteration < iterations && failures == 0; ++iteration)
	{
		// create_random
		u32 count{ random.next() % 20 };
		if (entities.empty()) count = min_population;
		while (count > 0)
		{
			// 生成順の通し番号をタグにする(f32で正確に表せる範囲に収まる)
			const f32 tag{ (f32)added };
			transform::init_info info{ make_transform_info(tag, 0.f, 0.f) };
			const game_entity::entity entity{ create_entity(info) };
			if (!entity.is_valid())
			{
				++failures;
				break;
			}
			entities.push_back({ entity, tag });
			if (!game_entity::is_alive(entity.get_id())) ++failures;
			++added;
			--count;
		}

		if (failures != 0) break;

		// remove_random
		count = random.next() % 20;
		if (entities.size() < min_population) continue;
		while (count > 0)
		{
			const u32 index{ random.next() % (u32)entities.size() };
			const game_entity::entity entity{ entities[index].entity };
			game_entity::remove(entity.get_id());
			if (game_entity::is_alive(entity.get_id())) ++failures;
			entities[index] = entities.back();
			entities.pop_back();
			++removed;
			--count;
		}
	}
	CHECK(failures == 0);
	// インデックス再利用が実際に発生する規模で回っていること
	CHECK(removed > id::min_deleted_elements);
	CHECK(added - removed == (u32)entities.size());

	// 生き残りはすべて生存しており、再利用を経ても自分のTransformデータを保っている
	u32 mismatches{ 0 };
	for (const tracked_entity& tracked : entities)
	{
		if (!game_entity::is_alive(tracked.entity.get_id())
			|| tracked.entity.position().x != tracked.tag)
		{
			++mismatches;
		}
	}
	CHECK(mismatches == 0);

	for (const tracked_entity& tracked : entities)
	{
		if (game_entity::is_alive(tracked.entity.get_id()))
		{
			game_entity::remove(tracked.entity.get_id());
		}
	}
	for (const tracked_entity& tracked : entities)
	{
		CHECK(!game_entity::is_alive(tracked.entity.get_id()));
	}
}
