// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_Vector.cpp]
// 作成日 : 2026/10/18
// 作成者 : 田中ミノル
// 概要 :
// Utilities/Vector.hの自作コンテナのテスト
// 更新履歴
// 2026/10/18 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"
#include "CommonHeaders.h"

using namespace dxforge;

namespace
{
	// 構築・破棄の回数を数える型。
	// NOTE: vector::reserve()はrealloc()で要素を再配置し、ムーブコンストラクタも
	//       デストラクタも呼ばない。そのため要素型は自己参照を持たない
	//       (trivially relocatableな)型である必要がある。この型もその条件を満たす。
	struct tracked
	{
		inline static u32 constructed{ 0 };
		inline static u32 destructed{ 0 };

		static void reset()
		{
			constructed = 0;
			destructed = 0;
		}

		u32 value{ 0 };

		tracked() { ++constructed; }
		explicit tracked(u32 v) : value{ v } { ++constructed; }
		tracked(const tracked& o) : value{ o.value } { ++constructed; }
		tracked(tracked&& o) noexcept : value{ o.value } { ++constructed; }
		tracked& operator=(const tracked&) = default;
		tracked& operator=(tracked&&) = default;
		~tracked() { ++destructed; }
	};
}

// _/_/_/_/_/_/_/_/ 基本的な要素の追加と参照 _/_/_/_/_/_/_/_/

DXFORGE_TEST_CASE(vector_push_back_reports_size_and_accessors)
{
	utl::vector<u32> v{};
	CHECK(v.empty());
	CHECK(v.size() == 0);

	v.push_back(10);
	v.push_back(20);
	v.push_back(30);

	CHECK(!v.empty());
	CHECK(v.size() == 3);
	CHECK(v[0] == 10);
	CHECK(v[1] == 20);
	CHECK(v[2] == 30);
	CHECK(v.front() == 10);
	CHECK(v.back() == 30);
	CHECK(v.data() != nullptr);
}

DXFORGE_TEST_CASE(vector_emplace_back_returns_reference_to_the_new_element)
{
	utl::vector<u32> v{};
	u32& added{ v.emplace_back(7) };
	CHECK(added == 7);

	// 戻り値はコンテナ内の要素への参照なので、書き換えが反映される。
	added = 99;
	CHECK(v[0] == 99);
	CHECK(v.size() == 1);
}

DXFORGE_TEST_CASE(vector_preserves_elements_across_reallocation)
{
	// 容量を超えて追加し続け、realloc()による再配置をまたいでも
	// 全要素が保持されることを確認する。
	constexpr u32 count{ 200 };
	utl::vector<u32> v{};

	for (u32 i{ 0 }; i < count; ++i)
	{
		v.emplace_back(i * 3);
	}

	REQUIRE(v.size() == count);
	CHECK(v.capacity() >= v.size());

	bool all_match{ true };
	for (u32 i{ 0 }; i < count; ++i)
	{
		if (v[i] != i * 3) all_match = false;
	}
	CHECK(all_match);
}

DXFORGE_TEST_CASE(vector_range_for_visits_every_element)
{
	utl::vector<u32> v{};
	v.push_back(1);
	v.push_back(2);
	v.push_back(3);
	v.push_back(4);

	u32 sum{ 0 };
	u32 visited{ 0 };
	for (const u32 value : v)
	{
		sum += value;
		++visited;
	}

	CHECK(visited == 4);
	CHECK(sum == 10);
	CHECK((u64)(v.end() - v.begin()) == v.size());
}

// _/_/_/_/_/_/_/_/ 容量とサイズの操作 _/_/_/_/_/_/_/_/

DXFORGE_TEST_CASE(vector_reserve_grows_capacity_without_changing_size)
{
	utl::vector<u32> v{};
	v.push_back(1);

	v.reserve(64);
	CHECK(v.capacity() >= 64);
	CHECK(v.size() == 1);
	CHECK(v[0] == 1);

	// 現在の容量より小さい値を渡した場合は何もしない。
	const u64 capacity_before{ v.capacity() };
	v.reserve(2);
	CHECK(v.capacity() == capacity_before);
}

DXFORGE_TEST_CASE(vector_clear_resets_size_but_keeps_capacity)
{
	utl::vector<u32> v{};
	v.push_back(1);
	v.push_back(2);
	const u64 capacity_before{ v.capacity() };

	v.clear();

	CHECK(v.empty());
	CHECK(v.size() == 0);
	// clear()は容量を解放しない。
	CHECK(v.capacity() == capacity_before);
}

DXFORGE_TEST_CASE(vector_resize_grows_with_default_values_and_shrinks)
{
	utl::vector<u32> v{};
	v.resize(3);
	REQUIRE(v.size() == 3);
	CHECK(v[0] == 0);
	CHECK(v[1] == 0);
	CHECK(v[2] == 0);

	v[0] = 5;
	v.resize(1);
	CHECK(v.size() == 1);
	CHECK(v[0] == 5);

	v.resize(1);  // 同サイズの指定は何も変えない
	CHECK(v.size() == 1);
}

DXFORGE_TEST_CASE(vector_resize_with_value_fills_the_new_elements)
{
	utl::vector<u32> v{};
	v.push_back(1);
	v.resize(4, 8);

	REQUIRE(v.size() == 4);
	CHECK(v[0] == 1);
	CHECK(v[1] == 8);
	CHECK(v[2] == 8);
	CHECK(v[3] == 8);
}

DXFORGE_TEST_CASE(vector_count_constructor_initializes_elements)
{
	const utl::vector<u32> defaulted{ (u64)3 };
	REQUIRE(defaulted.size() == 3);
	CHECK(defaulted[0] == 0);
	CHECK(defaulted[2] == 0);

	const utl::vector<u32> filled{ (u64)2, 4 };
	REQUIRE(filled.size() == 2);
	CHECK(filled[0] == 4);
	CHECK(filled[1] == 4);
}

// _/_/_/_/_/_/_/_/ 要素の削除 _/_/_/_/_/_/_/_/

DXFORGE_TEST_CASE(vector_erase_preserves_the_order_of_remaining_elements)
{
	utl::vector<u32> v{};
	v.push_back(10);
	v.push_back(20);
	v.push_back(30);
	v.push_back(40);

	u32* const position{ v.erase((u64)1) };

	REQUIRE(v.size() == 3);
	CHECK(v[0] == 10);
	CHECK(v[1] == 30);
	CHECK(v[2] == 40);
	// 戻り値は削除位置を指し、そこには後続の要素が詰められている。
	CHECK(*position == 30);
}

DXFORGE_TEST_CASE(vector_erase_of_the_last_element_only_shrinks)
{
	utl::vector<u32> v{};
	v.push_back(10);
	v.push_back(20);

	v.erase(v.size() - 1);

	REQUIRE(v.size() == 1);
	CHECK(v[0] == 10);
	CHECK(v.back() == 10);
}

DXFORGE_TEST_CASE(vector_erase_unordered_moves_the_last_element_into_the_hole)
{
	utl::vector<u32> v{};
	v.push_back(10);
	v.push_back(20);
	v.push_back(30);
	v.push_back(40);

	u32* const position{ v.erase_unordered((u64)1) };

	REQUIRE(v.size() == 3);
	CHECK(v[0] == 10);
	// erase()と違い順序は保たれず、末尾の要素が穴に入る。
	CHECK(v[1] == 40);
	CHECK(v[2] == 30);
	CHECK(*position == 40);
}

// _/_/_/_/_/_/_/_/ コピーとムーブ _/_/_/_/_/_/_/_/

DXFORGE_TEST_CASE(vector_copy_assignment_produces_an_independent_copy)
{
	utl::vector<u32> source{};
	source.push_back(1);
	source.push_back(2);

	utl::vector<u32> copy{};
	copy.push_back(99);  // 既存の要素は破棄される
	copy = source;

	REQUIRE(copy.size() == 2);
	CHECK(copy[0] == 1);
	CHECK(copy[1] == 2);

	// 双方が独立していることを確認する。
	copy[0] = 100;
	CHECK(source[0] == 1);
	CHECK(source.size() == 2);
}

DXFORGE_TEST_CASE(vector_copy_constructor_duplicates_elements)
{
	utl::vector<u32> source{};
	source.push_back(3);
	source.push_back(4);

	utl::vector<u32> copy{ source };

	REQUIRE(copy.size() == 2);
	CHECK(copy[0] == 3);
	CHECK(copy[1] == 4);
	CHECK(copy.data() != source.data());
}

DXFORGE_TEST_CASE(vector_move_constructor_leaves_the_source_empty)
{
	utl::vector<u32> source{};
	source.push_back(1);
	source.push_back(2);

	utl::vector<u32> moved{ std::move(source) };

	REQUIRE(moved.size() == 2);
	CHECK(moved[0] == 1);
	CHECK(moved[1] == 2);

	// ムーブ元はリセットされ、所有権を手放している。
	CHECK(source.size() == 0);
	CHECK(source.capacity() == 0);
	CHECK(source.data() == nullptr);
}

DXFORGE_TEST_CASE(vector_move_assignment_transfers_ownership)
{
	utl::vector<u32> source{};
	source.push_back(7);
	source.push_back(8);

	utl::vector<u32> target{};
	target.push_back(99);
	target = std::move(source);

	REQUIRE(target.size() == 2);
	CHECK(target[0] == 7);
	CHECK(target[1] == 8);
	CHECK(source.size() == 0);
	CHECK(source.data() == nullptr);
}

DXFORGE_TEST_CASE(vector_swap_exchanges_contents)
{
	utl::vector<u32> a{};
	a.push_back(1);

	utl::vector<u32> b{};
	b.push_back(2);
	b.push_back(3);

	a.swap(b);

	REQUIRE(a.size() == 2);
	CHECK(a[0] == 2);
	CHECK(a[1] == 3);

	REQUIRE(b.size() == 1);
	CHECK(b[0] == 1);
}

// _/_/_/_/_/_/_/_/ destructテンプレート引数による破棄の制御 _/_/_/_/_/_/_/_/

DXFORGE_TEST_CASE(vector_destructs_elements_when_destruct_is_true)
{
	tracked::reset();
	{
		utl::vector<tracked> v{};
		v.emplace_back(1u);
		v.emplace_back(2u);
		CHECK(tracked::constructed == 2);
		CHECK(tracked::destructed == 0);

		v.clear();
		// destruct == true なので要素のデストラクタが呼ばれる。
		CHECK(tracked::destructed == 2);
	}
	CHECK(tracked::destructed == 2);
}

DXFORGE_TEST_CASE(vector_destructs_the_tail_when_shrinking)
{
	tracked::reset();
	{
		utl::vector<tracked> v{};
		v.emplace_back(1u);
		v.emplace_back(2u);
		v.emplace_back(3u);
		REQUIRE(tracked::constructed == 3);

		v.resize(1);
		// 切り捨てられた2要素だけが破棄される。
		CHECK(tracked::destructed == 2);
		CHECK(v.size() == 1);
	}
	// 残っていた1要素もスコープ終了で破棄される。
	CHECK(tracked::destructed == 3);
}

DXFORGE_TEST_CASE(vector_erase_destructs_the_removed_element)
{
	tracked::reset();
	{
		utl::vector<tracked> v{};
		v.emplace_back(1u);
		v.emplace_back(2u);
		REQUIRE(tracked::destructed == 0);

		v.erase_unordered((u64)0);
		CHECK(tracked::destructed == 1);
		CHECK(v.size() == 1);
		CHECK(v[0].value == 2u);
	}
}

DXFORGE_TEST_CASE(vector_skips_destructors_when_destruct_is_false)
{
	// NOTE: free_listが内部で使っているのはこちらの指定(utl::vector<T, false>)で、
	//       要素のデストラクタが呼ばれないことに依存している。
	tracked::reset();
	{
		utl::vector<tracked, false> v{};
		v.emplace_back(1u);
		v.emplace_back(2u);
		REQUIRE(tracked::constructed == 2);

		v.clear();
		CHECK(v.size() == 0);
		// destruct == false なのでデストラクタは呼ばれない。
		CHECK(tracked::destructed == 0);
	}
	// スコープを抜けても呼ばれない。
	CHECK(tracked::destructed == 0);
}
