// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [RenderItem.cpp]
// 作成日 : 2025/01/13
// 作成者 : 田中ミノル
// 概要 :
// 更新履歴
// 2025/01/13 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include <filesystem>
#include "CommonHeaders.h"
#include "Content/ContentToEngine.h"
#include "Graphics/Renderer.h"
#include "ShaderCompilation.h"
#include "Components/Entity.h"
#include "../ContentTools/Geometry.h"

using namespace dxforge;

bool read_file(std::filesystem::path, std::unique_ptr<u8[]>&, u64&);

game_entity::entity create_one_game_entity(math::v3 position, math::v3a rotation, const char* script_name);
void remove_game_entity(game_entity::entity_id id);

namespace
{
	id::id_type fan_model_id{ id::invalid_id };
	id::id_type int_model_id{ id::invalid_id };
	id::id_type lab_model_id{ id::invalid_id };

	id::id_type fan_item_id{ id::invalid_id };
	id::id_type int_item_id{ id::invalid_id };
	id::id_type lab_item_id{ id::invalid_id };

	game_entity::entity_id fan_entity_id{ id::invalid_id };
	game_entity::entity_id int_entity_id{ id::invalid_id };
	game_entity::entity_id lab_entity_id{ id::invalid_id };

	id::id_type vs_id{ id::invalid_id };
	id::id_type ps_id{ id::invalid_id };
	id::id_type mtl_id{ id::invalid_id };

	std::unordered_map<id::id_type, game_entity::entity_id> render_item_entity_map;

	[[nodiscard]] id::id_type load_model(const char* path)
	{
		std::unique_ptr<u8[]> model;
		u64 size{ 0 };
		bool is_file = read_file(path, model, size);
		if (!is_file)
		{
			// エラーメッセージを構築する
			char buffer[256];
			sprintf_s(buffer, "File not found: %s", path);
			MessageBoxA(nullptr, buffer, "Error", MB_OK);
		}
		const id::id_type model_id = content::create_resource(model.get(), content::asset_type::mesh);
		assert(id::is_valid(model_id));
		return model_id;
	}

	void load_shaders()
	{
		// マテリアルがバーテックスシェーダーとピクセルシェーダーを使うとしよう。
		shader_file_info info;
		info.file_name = "TestShader.hlsl";
		info.function = "TestShaderVS";
		info.type = shader_type::vertex;

		const char* sharder_path{ "..\\..\\enginetest\\" };

		std::wstring defines[]{ L"ELEMENTS_TYPE=1", L"ELEMENTS_TYPE=3" };
		utl::vector<u32> keys;
		keys.emplace_back(tools::elements::elements_type::static_normal);
		keys.emplace_back(tools::elements::elements_type::static_normal_texture);

		utl::vector<std::wstring> extra_args{};
		utl::vector<std::unique_ptr<u8[]>> vertex_shader;
		utl::vector<const u8*> vertex_shader_pointers;
		for (u32 i{ 0 }; i < _countof(defines); ++i)
		{
			extra_args.clear();
			extra_args.emplace_back(L"-D");
			extra_args.emplace_back(defines[i]);
			vertex_shader.emplace_back(std::move(compile_shader(info, sharder_path, extra_args)));
			assert(vertex_shader.back().get());
			vertex_shader_pointers.emplace_back(vertex_shader.back().get());
		}

		info.function = "TestShaderPS";
		info.type = shader_type::pixel;

		auto pixel_shader = compile_shader(info, sharder_path, extra_args);
		assert(pixel_shader.get());

		vs_id = content::add_shader_group(vertex_shader_pointers.data(), (u32)vertex_shader_pointers.size(), keys.data());
		const u8* pixel_shaders[]{ pixel_shader.get() };
		ps_id = content::add_shader_group(&pixel_shaders[0], 1, &u32_invalid_id);
	}

	void create_material()
	{
		assert(id::is_valid(vs_id) && id::is_valid(ps_id));
		graphics::material_init_info info{};
		info.shader_ids[shader_type::vertex] = vs_id;
		info.shader_ids[shader_type::pixel] = ps_id;
		info.type = graphics::material_type::opaque;
		mtl_id = content::create_resource(&info, content::asset_type::material);
	}

	void remove_item(id::id_type item_id, id::id_type model_id)
	{
		if (id::is_valid(item_id))
		{
			graphics::remove_render_item(item_id);
			auto pair = render_item_entity_map.find(item_id);
			if (pair != render_item_entity_map.end())
			{
				remove_game_entity(pair->second);
			}

			if (id::is_valid(model_id))
			{
				content::destroy_resource(model_id, content::asset_type::mesh);
			}
		}
	}

}	// 匿名名前空間

void create_render_items()
{
	// NOTE: これらのモデルは、Primal Engineのパトロンサポーターであれば購入できる。
	// あなたが入手可能なものであれば、どれでも代用できる。
	auto _1 = std::thread{ [] {lab_model_id = load_model("..\\..\\x64\\lab_model.model"); } };
	auto _2 = std::thread{ [] {fan_model_id = load_model("..\\..\\x64\\fan_model.model"); } };
	auto _3 = std::thread{ [] {int_model_id = load_model("..\\..\\x64\\int_model.model"); } };
	auto _4 = std::thread{ [] {load_shaders(); } };

	lab_entity_id = create_one_game_entity({}, {}, nullptr).get_id();
	fan_entity_id = create_one_game_entity({ -10.47f, 5.93f, -6.7f }, {}, "fan_script").get_id();
	int_entity_id = create_one_game_entity({ 0.0f, 1.3f, -6.6f }, {}, "wibbly_wobbly_script").get_id();

	_1.join();
	_2.join();
	_3.join();
	_4.join();

	// NOTE: マテリアルを作成する前に、シェーダーを準備する必要があります。
	create_material();
	id::id_type materials[]{ mtl_id };

	lab_item_id = graphics::add_render_item(lab_entity_id, lab_model_id, _countof(materials), &materials[0]);
	fan_item_id = graphics::add_render_item(fan_entity_id, fan_model_id, _countof(materials), &materials[0]);
	int_item_id = graphics::add_render_item(int_entity_id, int_model_id, _countof(materials), &materials[0]);

	render_item_entity_map[lab_item_id] = lab_entity_id;
	render_item_entity_map[fan_item_id] = fan_entity_id;
	render_item_entity_map[int_item_id] = int_entity_id;
}

void destroy_render_items()
{
	remove_item(lab_item_id, lab_model_id);
	remove_item(fan_item_id, fan_model_id);
	remove_item(int_item_id, int_model_id);

	// remove material
	if (id::is_valid(mtl_id))
	{
		content::destroy_resource(mtl_id, content::asset_type::material);
	}

	// remove shaders and textures
	if (id::is_valid(vs_id))
	{
		content::remove_shader_group(vs_id);
	}
	if (id::is_valid(ps_id))
	{
		content::remove_shader_group(ps_id);
	}
}

void get_render_items(id::id_type* items, [[maybe_unused]] u32 count)
{
	assert(count == 3);
	items[0] = lab_item_id;
	items[1] = fan_item_id;
	items[2] = int_item_id;
}