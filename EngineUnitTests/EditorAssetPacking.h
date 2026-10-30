// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [EditorAssetPacking.h]
// 作成日 : 2026/10/30
// 作成者 : 田中ミノル
// 概要
// 　エディタのアセットの PackForEngine() と同じ並びのバイト列を作る(DLLの CreateResource に渡すデータのテスト用)
// 更新履歴
// 2026/10/30 新規作成 (Primal Day 077, #30)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "Graphics/Renderer.h"
#include <cstring>
#include <vector>

namespace dxforge::test
{
	/// @brief C# の BinaryWriter と同じく、値をアラインメントなしで後ろに足す
	class blob_writer
	{
	public:
		template<typename T>
		void write(T value)
		{
			const size_t offset{ _data.size() };
			_data.resize(offset + sizeof(T));
			memcpy(&_data[offset], &value, sizeof(T));
		}

		void write(const void* data, size_t size)
		{
			const u8* const bytes{ (const u8*)data };
			_data.insert(_data.end(), bytes, bytes + size);
		}

		[[nodiscard]] std::vector<u8>& data() { return _data; }

	private:
		std::vector<u8> _data;
	};

	/// @brief エディタの AppliedMaterial.PackForEngine() と同じ順で書く(並びは DXForgeEngineDLL/ResourceUpload.h)
	inline std::vector<u8> pack_material(const graphics::material_surface& surface, graphics::material_type::type type,
		const id::id_type* texture_ids, u32 texture_count, const id::id_type(&shader_ids)[graphics::shader_type::count])
	{
		blob_writer writer{};
		writer.write(texture_count);
		for (u32 i{ 0 }; i < texture_count; ++i) writer.write(texture_ids[i]);
		writer.write((u64)0);	// texture_ids のポインタ(writer.Write(IntPtr.Zero))
		// MaterialSurface.ToBinary()
		writer.write(surface.base_color.x); writer.write(surface.base_color.y); writer.write(surface.base_color.z); writer.write(surface.base_color.w);
		writer.write(surface.emissive.x); writer.write(surface.emissive.y); writer.write(surface.emissive.z);
		writer.write(surface.emissive_intensity);
		writer.write(surface.metallic);
		writer.write(surface.roughness);
		writer.write((u32)type);
		writer.write(texture_count);
		for (u32 i{ 0 }; i < graphics::shader_type::count; ++i) writer.write(shader_ids[i]);
		return std::move(writer.data());
	}

	/// @brief エディタの Texture.PackForEngine() と同じ並びの、ミップ1つ・配列1つの2Dテクスチャ
	inline std::vector<u8> pack_texture_2d(u32 width, u32 height, u32 format, u32 row_pitch, const std::vector<u8>& pixels)
	{
		blob_writer writer{};
		writer.write(width);
		writer.write(height);
		writer.write((u32)1);	// array_size
		writer.write((u32)0);	// flags
		writer.write((u32)1);	// mip_levels
		writer.write(format);
		writer.write(row_pitch);
		writer.write((u32)pixels.size());	// slice_pitch
		writer.write(pixels.data(), pixels.size());
		return std::move(writer.data());
	}
}
