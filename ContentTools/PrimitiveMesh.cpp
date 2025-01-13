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
		using namespace math;		// 数学関数
		using namespace DirectX;	// DirectX関数
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

		mesh create_uv_sphere(const primitive_init_info& info)
		{
			const u32 phi_count{ clamp(info.segments[axis::x], 3u, 64u) };
			const u32 theta_count{ clamp(info.segments[axis::y], 2u, 64u) };
			const f32 theta_step{ pi / theta_count };
			const f32 phi_step{ two_pi / phi_count };
			const u32 num_indices{ 2 * 3 * phi_count + 2 * 3 * phi_count * (theta_count - 2) };
			const u32 num_vertices{ 2 + phi_count * (theta_count - 1) };

			mesh m{};
			m.name = "uv_sphere";
			m.positions.resize(num_vertices);

			// 一番上の頂点を追加する
			u32 c{ 0 };
			m.positions[c++] = { 0.0f, info.size.y, 0.0f };

			// 中間の頂点を追加する
			for (u32 j{ 1 }; j <= (theta_count - 1); ++j)
			{
				const f32 theta{ j * theta_step };
				for (u32 i{ 0 }; i < phi_count; ++i)
				{
					const f32 phi{ i * phi_step };
					m.positions[c++] =
					{
						info.size.x * XMScalarSin(theta) * XMScalarCos(phi),
						info.size.y * XMScalarCos(theta),
						-info.size.z * XMScalarSin(theta) * XMScalarSin(phi)
					};
				}
			}

			// 一番下の頂点を追加する
			m.positions[c++] = { 0.0f,-info.size.y,0.0f };
			assert(c == num_vertices);

			// インデックス生成
			c = 0;
			m.raw_indices.resize(num_indices);
			utl::vector<v2> uvs(num_indices);
			const f32 inv_theta_count{ 1.0f / theta_count };
			const f32 inv_phi_count{ 1.0f / phi_count };

			// 一番上の頂点と第1リングを結ぶトップキャップの指標
			for (u32 i{ 0 }; i < (phi_count - 1); ++i)
			{
				uvs[c] = { (2 * i + 1) * 0.5f * inv_phi_count, 1.0f };
				m.raw_indices[c++] = 0;
				uvs[c] = { i * inv_phi_count, 1.0f - inv_theta_count };
				m.raw_indices[c++] = i + 1;
				uvs[c] = { (i + 1) * inv_phi_count, 1.0f - inv_theta_count };
				m.raw_indices[c++] = i + 2;
			}

			uvs[c] = { 1.0f - 0.5f * inv_phi_count, 1.0f };
			m.raw_indices[c++] = 0;
			uvs[c] = { 1.0f - inv_phi_count, 1.0f - inv_theta_count };
			m.raw_indices[c++] = phi_count;
			uvs[c] = { 1.0f, 1.0f - inv_theta_count };
			m.raw_indices[c++] = 1;

			// トップリングとボトムリングの間の距離の指標
			for (u32 j{ 0 }; j < (theta_count - 2); ++j)
			{
				for (u32 i{ 0 }; i < (phi_count - 1); ++i)
				{
					const u32 index[4]
					{
						1 + i + j * phi_count,
						1 + i + (j + 1) * phi_count,
						1 + (i + 1) + (j + 1) * phi_count,
						1 + (i + 1) + j * phi_count
					};

					uvs[c] = { i * inv_phi_count, 1.0f - (j + 1) * inv_theta_count };
					m.raw_indices[c++] = index[0];
					uvs[c] = { i * inv_phi_count, 1.0f - (j + 2) * inv_theta_count };
					m.raw_indices[c++] = index[1];
					uvs[c] = { (i + 1) * inv_phi_count, 1.0f - (j + 2) * inv_theta_count };
					m.raw_indices[c++] = index[2];

					uvs[c] = { i * inv_phi_count, 1.0f - (j + 1) * inv_theta_count };
					m.raw_indices[c++] = index[0];
					uvs[c] = { (i + 1) * inv_phi_count, 1.0f - (j + 2) * inv_theta_count };
					m.raw_indices[c++] = index[2];
					uvs[c] = { (i + 1) * inv_phi_count, 1.0f - (j + 1) * inv_theta_count };
					m.raw_indices[c++] = index[3];
				}

				const u32 index[4]
				{
					phi_count + j * phi_count,
					phi_count + (j + 1) * phi_count,
					1 + (j + 1) * phi_count,
					1 + j * phi_count
				};

				uvs[c] = { 1.0f - inv_phi_count, 1.0f - (j + 1) * inv_theta_count };
				m.raw_indices[c++] = index[0];
				uvs[c] = { 1.0f - inv_phi_count, 1.0f - (j + 2) * inv_theta_count };
				m.raw_indices[c++] = index[1];
				uvs[c] = { 1.0f, 1.0f - (j + 2) * inv_theta_count };
				m.raw_indices[c++] = index[2];

				uvs[c] = { 1.0f - inv_phi_count, 1.0f - (j + 1) * inv_theta_count };
				m.raw_indices[c++] = index[0];
				uvs[c] = { 1.0f, 1.0f - (j + 2) * inv_theta_count };
				m.raw_indices[c++] = index[2];
				uvs[c] = { 1.0f, 1.0f - (j + 1) * inv_theta_count };
				m.raw_indices[c++] = index[3];
			}

			// ボトルキャップのインデックス、南ポゼッションと最後のリングを結ぶ
			const u32 south_pole_index{ (u32)m.positions.size() - 1 };
			for (u32 i{ 0 }; i < (phi_count - 1); ++i)
			{
				uvs[c] = { (2 * i + 1) * 0.5f * inv_phi_count, 0.0f };
				m.raw_indices[c++] = south_pole_index;
				uvs[c] = { (i + 1) * inv_phi_count, inv_theta_count };
				m.raw_indices[c++] = south_pole_index - phi_count + i + 1;
				uvs[c] = { i * inv_phi_count, inv_theta_count };
				m.raw_indices[c++] = south_pole_index - phi_count + i;
			}

			uvs[c] = { 1.0f - 0.5f * inv_phi_count, 0.0f };
			m.raw_indices[c++] = south_pole_index;
			uvs[c] = { 1.0f, inv_theta_count };
			m.raw_indices[c++] = south_pole_index - phi_count;
			uvs[c] = { 1.0f - inv_phi_count, inv_theta_count };
			m.raw_indices[c++] = south_pole_index - 1;

			assert(c == num_indices);

			m.uv_sets.emplace_back(uvs);

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
		void create_cube(scene& /*scene*/, const primitive_init_info& /*info*/)
		{
		}

		// UV球
		void create_uv_sphere(scene& scene, const primitive_init_info& info)
		{
			lod_group lod{};
			lod.name = "uv_sphere";
			lod.meshes.emplace_back(create_uv_sphere(info));
			scene.lod_groups.emplace_back(lod);
		}

		// ICO球
		void create_ico_sphere(scene& /*scene*/, const primitive_init_info& /*info*/)
		{
		}

		// 円柱
		void create_cylinder(scene& /*scene*/, const primitive_init_info& /*info*/)
		{
		}

		// カプセル
		void create_capsule(scene& /*scene*/, const primitive_init_info& /*info*/)
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


