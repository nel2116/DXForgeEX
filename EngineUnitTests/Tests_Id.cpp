// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_Id.cpp]
// 作成日 : 2026/10/18
// 作成者 : 田中ミノル
// 概要 :
// Common/Id.hの世代付きIDロジックのテスト
// 更新履歴
// 2026/10/18 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"
#include "CommonHeaders.h"

using namespace dxforge;

DXFORGE_TEST_CASE(id_index_and_generation_are_encoded_in_low_and_high_bits)
{
	constexpr id::id_type raw{ 42 }; // インデックス42、世代0
	CHECK(id::index(raw) == 42);
	CHECK(id::generation(raw) == 0);
}

DXFORGE_TEST_CASE(id_new_generation_preserves_index_and_increments_generation)
{
	constexpr id::id_type raw{ 42 };
	const id::id_type gen1{ id::new_generation(raw) };
	CHECK(id::index(gen1) == 42);
	CHECK(id::generation(gen1) == 1);

	const id::id_type gen2{ id::new_generation(gen1) };
	CHECK(id::index(gen2) == 42);
	CHECK(id::generation(gen2) == 2);
}

DXFORGE_TEST_CASE(id_is_valid_rejects_only_invalid_id)
{
	CHECK(id::is_valid(0));
	CHECK(id::is_valid(42));
	CHECK(!id::is_valid(id::invalid_id));
}

DEFINE_TYPED_ID(unit_test_id);

DXFORGE_TEST_CASE(defined_typed_id_round_trips_through_id_type)
{
	const unit_test_id typed{ 7 };
	CHECK((id::id_type)typed == 7);
	CHECK(id::is_valid(typed));

	const unit_test_id invalid{ id::invalid_id };
	CHECK(!id::is_valid(invalid));
}
