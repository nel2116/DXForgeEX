// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_ResourceUpload.cpp]
// 作成日 : 2026/10/30
// 作成者 : 田中ミノル
// 概要 :
// DXForgeEngineDLL/ResourceUpload.cpp(DLLの CreateResource / DestroyResource)のうち、GPUの要らない部分のテスト。
// リソースを実際に作って破棄するのは、--render のヘッドレス実行(HeadlessRender.cpp)で確かめる。
// 更新履歴
// 2026/10/30 新規作成 (Primal Day 077, #30)
// 2026/11/09 material_surface の12バイトの並びと、float から詰めた値への変換のテストを追加 (Primal Day 087, #252)
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
		surface.base_color[0] = 10; surface.base_color[1] = 20; surface.base_color[2] = 30; surface.base_color[3] = 40;
		surface.emissive[0] = 50; surface.emissive[1] = 60; surface.emissive[2] = 70;
		surface.metallic = 64;
		surface.roughness = 191;
		surface.input_mask = 0b10101;
		surface.emissive_intensity = 0xabcd;
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
		return memcmp(&a, &b, sizeof(material_surface)) == 0;
	}
} // 匿名名前空間

// material_surface はシェーダーの MaterialData[0..2] とエディタの MaterialSurface.ToBinary(writer, asInteger: true) の並び
// (docs/design/material-surface-packing.md)
DXFORGE_TEST_CASE(ResourceUpload_MaterialSurfaceIsPacked12Bytes)
{
	CHECK(sizeof(material_surface) == 12);
	CHECK(offsetof(material_surface, base_color) == 0);
	CHECK(offsetof(material_surface, emissive) == 4);
	CHECK(offsetof(material_surface, metallic) == 7);
	CHECK(offsetof(material_surface, roughness) == 8);
	CHECK(offsetof(material_surface, input_mask) == 9);
	CHECK(offsetof(material_surface, emissive_intensity) == 10);

	// シェーダーは MaterialData[2] の 8..15 ビットを入力のマスク、16..31 ビットを emissive_intensity として読む
	const material_surface surface{ make_surface() };
	u32 words[3]{};
	memcpy(words, &surface, sizeof(words));
	CHECK((words[0] & 0xff) == 10 && ((words[0] >> 24) & 0xff) == 40);
	CHECK(((words[1] >> 24) & 0xff) == 64);
	CHECK((words[2] & 0xff) == 191);
	CHECK(((words[2] >> 8) & 0xff) == 0b10101);
	CHECK(((words[2] >> 16) & 0xffff) == 0xabcd);

	// 既定値は今までの float の既定値(白、metallic 0、roughness 1、emissive_intensity 1)に最も近い値
	const material_surface defaults{};
	CHECK(defaults.base_color[0] == 255 && defaults.base_color[3] == 255 && defaults.emissive[0] == 0);
	CHECK(defaults.metallic == 0 && defaults.roughness == 255 && defaults.input_mask == 0);
	CHECK(defaults.emissive_intensity == material_surface::to_emissive_intensity(1.f));
}

// float から詰めた値への変換は四捨五入(エディタの MaterialSurface.ToUnorm8 / ToEmissiveIntensity と同じ。material_asset_test.ps1)
DXFORGE_TEST_CASE(ResourceUpload_MaterialSurfaceConversionRounds)
{
	CHECK(material_surface::to_unorm8(0.f) == 0);
	CHECK(material_surface::to_unorm8(0.5f) == 128);	// 127.5 は切り上げ
	CHECK(material_surface::to_unorm8(0.4f) == 102);
	CHECK(material_surface::to_unorm8(0.9f) == 230);	// 229.5 は切り上げ
	CHECK(material_surface::to_unorm8(1.f) == 255);
	CHECK(material_surface::to_unorm8(-1.f) == 0);
	CHECK(material_surface::to_unorm8(2.f) == 255);

	CHECK(material_surface::to_emissive_intensity(0.f) == 0);
	CHECK(material_surface::to_emissive_intensity(1.f) == 7);	// 6.5535 -> 7(シェーダーでは約1.068)
	CHECK(material_surface::to_emissive_intensity(material_surface::max_emissive_intensity) == 65535);
	CHECK(material_surface::to_emissive_intensity(1e6f) == 65535);
}

// エディタの AppliedMaterial.PackForEngine() が書く大きさ: u32 + id * n + (u64 + material_surface(12) + u32 * 2 + id * 8)
DXFORGE_TEST_CASE(ResourceUpload_PackedMaterialSizeMatchesEditorLayout)
{
	id::id_type shader_ids[shader_type::count];
	make_shader_ids(shader_ids);
	const id::id_type texture_ids[]{ 11, 12 };

	CHECK(editor::packed_material_init_info_size == 8 + 12 + 2 * 4 + shader_type::count * 4);
	CHECK(editor::packed_material_size(0) == 64);
	CHECK(editor::packed_material_size(2) == 72);
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
	memcpy(&data[sizeof(u32) + sizeof(id::id_type) + 24], &wrong_count, sizeof(u32));
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
