// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ResourceUpload.cpp]
// 作成日 : 2026/10/30
// 作成者 : 田中ミノル
// 概要
// 　エディタから渡されたアセット(ジオメトリ・テクスチャ・マテリアル)のデータからエンジンのリソースを作る
// 更新履歴
// 2026/10/30 新規作成(Primal Day 077 の CreateResource / DestroyResource と、Day 078 のその実体。#30)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "ResourceUpload.h"

namespace dxforge::editor
{
	using namespace graphics;

	static_assert(sizeof(material_surface) == 40, "AppliedMaterial.PackForEngine() (MaterialSurface.ToBinary) writes 10 floats");
	static_assert(sizeof(material_init_info) == packed_material_init_info_size, "AppliedMaterial.PackForEngine() writes material_init_info without padding");
	static_assert(offsetof(material_init_info, surface) == 8 && offsetof(material_init_info, type) == 48 &&
		offsetof(material_init_info, texture_count) == 52 && offsetof(material_init_info, shader_ids) == 56,
		"AppliedMaterial.PackForEngine() writes material_init_info with these offsets");

	namespace
	{
		bool is_supported(u32 type)
		{
			return type == content::asset_type::material || type == content::asset_type::mesh || type == content::asset_type::texture;
		}
	} // 匿名名前空間

	bool unpack_material_data(const u8* data, material_init_info& info)
	{
		if (!data) return false;

		u32 texture_count{ 0 };
		memcpy(&texture_count, data, sizeof(u32));
		const id::id_type* const texture_ids{ (const id::id_type*)(data + sizeof(u32)) };
		memcpy(&info, data + sizeof(u32) + texture_count * sizeof(id::id_type), sizeof(material_init_info));
		info.texture_ids = texture_count ? const_cast<id::id_type*>(texture_ids) : nullptr;

		// NOTE: エンジンの add_material() は、以下を assert で確かめるだけなので、ここで弾く
		if (info.texture_count != texture_count || info.type >= material_type::count) return false;
		for (u32 i{ 0 }; i < texture_count; ++i)
		{
			if (!id::is_valid(texture_ids[i])) return false;
		}

		bool has_shader{ false };
		for (u32 i{ 0 }; i < shader_type::count; ++i)
		{
			has_shader |= id::is_valid(info.shader_ids[i]);
		}

		return has_shader;
	}

	id::id_type create_resource(const u8* data, u32 type)
	{
		if (!data || !is_supported(type)) return id::invalid_id;

		if (type == content::asset_type::material)
		{
			material_init_info info{};
			if (!unpack_material_data(data, info)) return id::invalid_id;
			return content::create_resource(&info, content::asset_type::material);
		}

		return content::create_resource(data, (content::asset_type::type)type);
	}

	void destroy_resource(id::id_type id, u32 type)
	{
		if (!id::is_valid(id) || !is_supported(type)) return;
		content::destroy_resource(id, (content::asset_type::type)type);
	}
}
