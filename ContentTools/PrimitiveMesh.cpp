// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [PrimitiveMesh.cpp]
// 作成日 : 2024/12/24
// 作成者 : 田中ミノル
// 概要 :
//
// 更新履歴
// 2024/12/24 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "PrimitiveMesh.h"
#include "Geometry.h"

namespace dxforge::tools
{
	namespace
	{
		using namespace math;	// 数学関数
		// 関数ポインタ型
		using primitive_mesh_creator = void(*)(scene&, const primitive_init_info& info);

		// プリミティブメッシュ生成関数
		void create_plane(scene& scene, const primitive_init_info& info);
		void create_cube(scene& scene, const primitive_init_info& info);
		void create_uv_sphere(scene& scene, const primitive_init_info& info);
		void create_ico_sphere(scene& scene, const primitive_init_info& info);
		void create_cylinder(scene& scene, const primitive_init_info& info);
		void create_capsule(scene& scene, const primitive_init_info& info);

		// プリミティブメッシュ生成関数配列
		primitive_mesh_creator creators[]
		{
			create_plane,
			create_cube,
			create_uv_sphere,
			create_ico_sphere,
			create_cylinder,
			create_capsule
		};

		// 配列の要素数がprimitive_mesh_type::countと一致していることを確認
		static_assert(_countof(creators) == primitive_mesh_type::count);

		struct axis
		{
			enum : u32
			{
				x = 0,
				y = 1,
				z = 2
			};
		};

		/// @brief 平面メッシュ生成
		/// @param info プリミティブ初期化情報
		/// @param horizontal_index 水平インデックス
		/// @param vertical_index 垂直インデックス
		/// @param flip_winding 面反転
		/// @param offset オフセット
		/// @param u_range u範囲
		/// @param v_range v範囲
		/// @return 生成されたメッシュ
		mesh create_plane(const primitive_init_info& info,
			u32 horizontal_index = axis::x, u32 vertical_index = axis::z, bool flip_winding = false,
			v3 offset = { -0.5f,0.0f,-0.5f }, v2 u_range = { 0.0f,1.0f }, v2 v_range = { 0.0f,1.0f })
		{
			// インデックスチェック
			assert(horizontal_index < 3 && vertical_index < 3);
			assert(horizontal_index != vertical_index);

			// セグメント数
			const u32 horizontal_count{ clamp(info.segments[horizontal_index],1u,10u) };
			const u32 vertical_count{ clamp(info.segments[vertical_index],1u,10u) };
			const f32 horizontal_step{ 1.0f / horizontal_count };
			const f32 vertical_step{ 1.0f / vertical_count };
			const f32 u_step{ (u_range.y - u_range.x) / horizontal_count };
			const f32 v_step{ (v_range.y - v_range.x) / vertical_count };

			// メッシュ生成
			mesh m{};
			utl::vector<v2> uvs;
			// 頂点生成
			for (u32 j{ 0 }; j <= vertical_count; ++j)
			{
				for (u32 i{ 0 }; i <= horizontal_count; ++i)
				{
					// 位置
					v3 position{ offset };
					f32* const as_array{ &position.x };
					as_array[horizontal_index] += i * horizontal_step;
					as_array[vertical_index] += j * vertical_step;
					m.positions.emplace_back(position.x * info.size.x, position.y * info.size.y, position.z * info.size.z);

					// UV座標
					v2 uv{ u_range.x,1.0f - v_range.x };
					uv.x += i * u_step;
					uv.y -= j * v_step;
					uvs.emplace_back(uv);
				}
			}

			assert(m.positions.size() == (((u64)horizontal_count + 1) * ((u64)vertical_count + 1)));

			// インデックス生成
			const u32 row_length{ horizontal_count + 1 };	// 1行の頂点数
			for (u32 j{ 0 }; j < vertical_count; ++j)
			{
				u32 k{ 0 };
				for (u32 i{ k }; i < horizontal_count; ++i)
				{
					const u32 index[4]
					{
						i + j * row_length,
						i + (j + 1) * row_length,
						(i + 1) + j * row_length,
						(i + 1) + (j + 1) * row_length
					};

					m.raw_indices.emplace_back(index[0]);
					m.raw_indices.emplace_back(index[flip_winding ? 2 : 1]);
					m.raw_indices.emplace_back(index[flip_winding ? 1 : 2]);

					m.raw_indices.emplace_back(index[2]);
					m.raw_indices.emplace_back(index[flip_winding ? 3 : 1]);
					m.raw_indices.emplace_back(index[flip_winding ? 1 : 3]);
				}
				++k;
			}
			// インデックス数
			const u32 num_indices{ 3 * 2 * horizontal_count * vertical_count };
			assert(m.raw_indices.size() == num_indices);

			m.uv_sets.resize(1);	// UVセット

			// UVセット
			for (u32 i{ 0 }; i < num_indices; ++i)
			{
				m.uv_sets[0].emplace_back(uvs[m.raw_indices[i]]);
			}

			return m;
		}

		// プリミティブメッシュ生成関数
		// 平面
		void create_plane(scene& scene, const primitive_init_info& info)
		{
			lod_group lod{};
			lod.name = "plane";
			lod.meshes.emplace_back(create_plane(info));
			scene.lod_groups.emplace_back(lod);
		}

		// 立方体
		void create_cube(scene& scene, const primitive_init_info& info)
		{
		}

		// UV球
		void create_uv_sphere(scene& scene, const primitive_init_info& info)
		{
		}

		// ICO球
		void create_ico_sphere(scene& scene, const primitive_init_info& info)
		{
		}

		// 円柱
		void create_cylinder(scene& scene, const primitive_init_info& info)
		{
		}

		// カプセル
		void create_capsule(scene& scene, const primitive_init_info& info)
		{
		}

	} // 匿名名前空間


	EDITOR_INTERFACE void CreatePrimitiveMesh(scene_data* data, primitive_init_info* info)
	{
		// 引数チェック
		assert(data && info);
		assert(info->type < primitive_mesh_type::count);

		// シーンデータの初期化
		scene scene{};
		// プリミティブメッシュ生成
		creators[info->type](scene, *info);

		data->settings.calculate_normals = 1;
		process_scene(scene, data->settings);
		pack_data(scene, *data);
	}





} // namespace dxforge::tools


