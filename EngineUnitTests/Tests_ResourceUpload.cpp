// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_ResourceUpload.cpp]
// 作成日 : 2026/10/30
// 作成者 : 田中ミノル
// 概要 :
// DXForgeEngineDLL/ResourceUpload.cpp(DLLの CreateResource / DestroyResource)のうち、GPUの要らない部分のテスト。
// リソースを実際に作って破棄するのは、--render のヘッドレス実行(HeadlessRender.cpp)で確かめる。
// 更新履歴
// 2026/10/30 新規作成 (Primal Day 077, #30)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"
#include "EditorAssetPacking.h"
#include "../DXForgeEngineDLL/ResourceUpload.h"

using namespace dxforge;
using namespace dxforge::graphics;

namespace
{
	material_surface make_surface()
	{
		material_surface surface{};
		surface.base_color = { 0.1f, 0.2f, 0.3f, 0.4f };
		surface.emissive = { 0.5f, 0.6f, 0.7f };
		surface.emissive_intensity = 2.f;
		surface.metallic = 0.25f;
		surface.roughness = 0.75f;
		return surface;
	}

	void make_shader_ids(id::id_type(&shader_ids)[shader_type::count])
	{
		for (id::id_type& id : shader_ids) id = id::invalid_id;
		shader_ids[shader_type::vertex] = 3;
		shader_ids[shader_type::pixel] = 7;
	}

	bool same_surface(const material_surface& a, const material_surface& b)
	{
		return a.base_color.x == b.base_color.x && a.base_color.y == b.base_color.y && a.base_color.z == b.base_color.z &&
			a.base_color.w == b.base_color.w && a.emissive.x == b.emissive.x && a.emissive.y == b.emissive.y &&
			a.emissive.z == b.emissive.z && a.emissive_intensity == b.emissive_intensity && a.metallic == b.metallic &&
			a.roughness == b.roughness;
	}
} // 匿名名前空間

// エディタの AppliedMaterial.PackForEngine() が書く大きさ: u32 + id * n + (u64 + f32 * 10 + u32 * 2 + id * 8)
DXFORGE_TEST_CASE(ResourceUpload_PackedMaterialSizeMatchesEditorLayout)
{
	id::id_type shader_ids[shader_type::count];
	make_shader_ids(shader_ids);
	const id::id_type texture_ids[]{ 11, 12 };

	CHECK(editor::packed_material_init_info_size == 8 + 10 * 4 + 2 * 4 + shader_type::count * 4);
	CHECK(editor::packed_material_size(0) == 92);
	CHECK(editor::packed_material_size(2) == 100);
	CHECK(test::pack_material(make_surface(), material_type::opaque, nullptr, 0, shader_ids).size() == editor::packed_material_size(0));
	CHECK(test::pack_material(make_surface(), material_type::opaque, texture_ids, 2, shader_ids).size() == editor::packed_material_size(2));
}

DXFORGE_TEST_CASE(ResourceUpload_UnpacksMaterialWithoutTextures)
{
	id::id_type shader_ids[shader_type::count];
	make_shader_ids(shader_ids);
	const std::vector<u8> data{ test::pack_material(make_surface(), material_type::opaque, nullptr, 0, shader_ids) };

	material_init_info info{};
	REQUIRE(editor::unpack_material_data(data.data(), info));
	CHECK(info.texture_ids == nullptr);
	CHECK(info.texture_count == 0);
	CHECK(info.type == material_type::opaque);
	CHECK(same_surface(info.surface, make_surface()));
	for (u32 i{ 0 }; i < shader_type::count; ++i)
	{
		CHECK(info.shader_ids[i] == shader_ids[i]);
	}
}

// テクスチャが2つだと material_init_info の位置は 4 + 4 * 2 = 12 で8バイトにそろわない。さらにデータ自体も1バイトずらす
DXFORGE_TEST_CASE(ResourceUpload_UnpacksMaterialWithTexturesAtUnalignedOffsets)
{
	id::id_type shader_ids[shader_type::count];
	make_shader_ids(shader_ids);
	const id::id_type texture_ids[]{ 11, 12 };
	const std::vector<u8> packed{ test::pack_material(make_surface(), material_type::opaque, texture_ids, 2, shader_ids) };
	std::vector<u8> data(packed.size() + 1);
	memcpy(&data[1], packed.data(), packed.size());

	material_init_info info{};
	REQUIRE(editor::unpack_material_data(&data[1], info));
	REQUIRE(info.texture_count == 2);
	CHECK((const u8*)info.texture_ids == &data[1 + sizeof(u32)]);
	CHECK(info.texture_ids[0] == 11);
	CHECK(info.texture_ids[1] == 12);
	CHECK(same_surface(info.surface, make_surface()));
	CHECK(info.shader_ids[shader_type::vertex] == 3);
	CHECK(info.shader_ids[shader_type::pixel] == 7);
	CHECK(!id::is_valid(info.shader_ids[shader_type::compute]));
}

DXFORGE_TEST_CASE(ResourceUpload_RejectsBrokenMaterialData)
{
	id::id_type shader_ids[shader_type::count];
	make_shader_ids(shader_ids);
	const id::id_type texture_ids[]{ 11, id::invalid_id };
	material_init_info info{};

	CHECK(!editor::unpack_material_data(nullptr, info));

	// 先頭のテクスチャの数と material_init_info の texture_count が違う
	std::vector<u8> data{ test::pack_material(make_surface(), material_type::opaque, texture_ids, 1, shader_ids) };
	CHECK(editor::unpack_material_data(data.data(), info));
	const u32 wrong_count{ 2 };
	memcpy(&data[sizeof(u32) + sizeof(id::id_type) + 52], &wrong_count, sizeof(u32));
	CHECK(!editor::unpack_material_data(data.data(), info));

	// テクスチャのIDが無効
	data = test::pack_material(make_surface(), material_type::opaque, texture_ids, 2, shader_ids);
	CHECK(!editor::unpack_material_data(data.data(), info));

	// マテリアルの種類が範囲外
	data = test::pack_material(make_surface(), material_type::count, nullptr, 0, shader_ids);
	CHECK(!editor::unpack_material_data(data.data(), info));

	// シェーダーが1つもない
	for (id::id_type& id : shader_ids) id = id::invalid_id;
	data = test::pack_material(make_surface(), material_type::opaque, nullptr, 0, shader_ids);
	CHECK(!editor::unpack_material_data(data.data(), info));
}

// 正しくないデータや扱わない種類は、エンジン(GPU)に渡す前に弾く
DXFORGE_TEST_CASE(ResourceUpload_CreateResourceRejectsWithoutTouchingTheEngine)
{
	const u8 dummy[editor::packed_material_size(0)]{};

	CHECK(!id::is_valid(editor::create_resource(nullptr, content::asset_type::mesh)));
	CHECK(!id::is_valid(editor::create_resource(dummy, content::asset_type::unknown)));
	CHECK(!id::is_valid(editor::create_resource(dummy, content::asset_type::animation)));
	CHECK(!id::is_valid(editor::create_resource(dummy, content::asset_type::audio)));
	CHECK(!id::is_valid(editor::create_resource(dummy, content::asset_type::skeleton)));
	CHECK(!id::is_valid(editor::create_resource(dummy, content::asset_type::count)));
	CHECK(!id::is_valid(editor::create_resource(dummy, 99)));
	// シェーダーが1つもないマテリアル
	id::id_type shader_ids[shader_type::count];
	for (id::id_type& id : shader_ids) id = id::invalid_id;
	const std::vector<u8> no_shader{ test::pack_material(make_surface(), material_type::opaque, nullptr, 0, shader_ids) };
	CHECK(!id::is_valid(editor::create_resource(no_shader.data(), content::asset_type::material)));

	// 無効なIDや扱わない種類の破棄は何もしない(assertにも当たらない)
	editor::destroy_resource(id::invalid_id, content::asset_type::mesh);
	editor::destroy_resource(0, content::asset_type::animation);
	editor::destroy_resource(0, 99);
}
