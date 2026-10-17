// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_FreeList.cpp]
// 作成日 : 2026/10/18
// 作成者 : 田中ミノル
// 概要 :
// Utilities/FreeList.hのインデックス再利用ロジックのテスト
// 更新履歴
// 2026/10/18 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"
#include "CommonHeaders.h"

using namespace dxforge;

namespace
{
	// NOTE: free_listはsizeof(T) >= sizeof(u32)を要求する。8バイトにすることで、
	// 削除済みスロットの再利用チェック(already_removed)も合わせて検証できる。
	struct test_item
	{
		explicit test_item(u32 value) : a{ value }, b{ value * 2 } {}
		u32 a;
		u32 b;
	};
}

DXFORGE_TEST_CASE(free_list_add_assigns_sequential_indices)
{
	utl::free_list<test_item> list{};
	const u32 id0{ list.add(10) };
	const u32 id1{ list.add(20) };

	CHECK(id0 == 0);
	CHECK(id1 == 1);
	CHECK(list.size() == 2);
	CHECK(list[id0].a == 10);
	CHECK(list[id1].a == 20);

	list.remove(id0);
	list.remove(id1);
}

DXFORGE_TEST_CASE(free_list_remove_then_add_reuses_the_freed_index)
{
	utl::free_list<test_item> list{};
	const u32 id0{ list.add(1) };
	const u32 id1{ list.add(2) };
	const u32 id2{ list.add(3) };
	CHECK(list.size() == 3);

	list.remove(id1);
	CHECK(list.size() == 2);

	const u32 reused{ list.add(99) };
	CHECK(reused == id1);
	CHECK(list[reused].a == 99);
	CHECK(list.size() == 3);

	list.remove(id0);
	list.remove(reused);
	list.remove(id2);
}

DXFORGE_TEST_CASE(free_list_empty_reports_correctly_before_and_after_use)
{
	utl::free_list<test_item> list{};
	CHECK(list.empty());

	const u32 id{ list.add(5) };
	CHECK(!list.empty());
	CHECK(list.capacity() >= 1);

	list.remove(id);
	CHECK(list.empty());
}
