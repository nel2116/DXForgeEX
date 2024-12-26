// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Geometry.cpp]
// 作成日 : 2024/12/26
// 作成者 : 田中ミノル
// 概要 :
//
// 更新履歴
// 2024/12/26 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Geometry.h"


namespace dxforge::tools
{
	namespace
	{
		using namespace math;		// 数学関数
		using namespace DirectX;	// DirectX関数

		/// @brief 法線の再計算
		/// @param m メッシュデータの参照
		void recalculate_normals(mesh& m)
		{
			const u32 num_indices{ (u32)m.raw_indices.size() };
			m.normals.resize(num_indices);

			for (u32 i{ 0 }; i < num_indices; ++i)
			{
				const u32 io{ m.raw_indices[i] };
				const u32 i1{ m.raw_indices[++i] };
				const u32 i2{ m.raw_indices[++i] };

				XMVECTOR v0{ XMLoadFloat3(&m.positions[io]) };
				XMVECTOR v1{ XMLoadFloat3(&m.positions[i1]) };
				XMVECTOR v2{ XMLoadFloat3(&m.positions[i2]) };

				XMVECTOR e0{ v1 - v0 };
				XMVECTOR e1{ v2 - v0 };
				XMVECTOR n{ XMVector3Normalize(XMVector3Cross(e0, e1)) };

				XMStoreFloat3(&m.normals[i], n);
				m.normals[i - 1] = m.normals[i];
				m.normals[i - 2] = m.normals[i];
			}
		};

		/// @brief 法線の処理
		/// @param m メッシュデータの参照
		/// @param smoothing_angle スムージング角度
		void process_normals(mesh& m, f32 smoothing_angle)
		{
			const f32 cos_alpha{ XMScalarCos(pi - smoothing_angle * pi / 180.0f) };				// スムージング角度の余弦
			const bool is_hard_edge{ XMScalarNearEqual(smoothing_angle, 180.0f, epsilon) };		// ハードエッジか
			const bool is_soft_edge{ XMScalarNearEqual(smoothing_angle, 0.0f, epsilon) };		// ソフトエッジか
			const u32 num_indices{ (u32)m.raw_indices.size() };									// インデックス数
			const u32 num_vertices{ (u32)m.positions.size() };									// 頂点数
			// インデックス数と頂点数が0でないことを確認
			assert(num_indices && num_vertices);

			// インデックスのリサイズ
			m.indices.resize(num_indices);

			utl::vector<utl::vector<u32>> idx_ref(num_vertices);	// 頂点参照インデックス
			// インデックスの参照を生成
			for (u32 i{ 0 }; i < num_indices; ++i)
				idx_ref[m.raw_indices[i]].emplace_back(i);

			// 法線の再計算
			for (u32 i{ 0 }; i < num_vertices; ++i)
			{
				// インデックス参照
				auto& refs{ idx_ref[i] };
				u32 num_refs{ (u32)refs.size() };
				for (u32 j{ 0 }; j < num_refs; ++j)
				{
					m.indices[refs[j]] = (u32)m.vertices.size();
					vertex& v{ m.vertices.emplace_back() };
					v.position = m.positions[m.raw_indices[refs[j]]];

					// 法線の計算
					XMVECTOR n1{ XMLoadFloat3(&m.normals[refs[j]]) };
					if (!is_hard_edge)
					{
						// インデックス参照のループ
						for (u32 k{ j + 1 }; k < num_refs; ++k)
						{
							// この値は法線間の角度の余弦を表す。
							f32 cos_theta{ 0.0f };
							XMVECTOR n2{ XMLoadFloat3(&m.normals[refs[k]]) };
							if (!is_soft_edge)
							{
								// NOTE: n1の長さはこのループの繰り返しで変化する可能性があるため、この計算ではn1の長さを考慮している。
								// n2の長さは単位とする。 cos(angle) = dot(n1,n2) / (||n1|| * ||n2||)
								XMStoreFloat(&cos_theta, XMVector3Dot(n1, n2) * XMVector3ReciprocalLength(n1));
							}

							// ソフトエッジの場合は法線を加算
							if (is_soft_edge || cos_theta >= cos_alpha)
							{
								n1 += n2;

								// インデックスの削除
								m.indices[refs[k]] = m.indices[refs[j]];
								refs.erase(refs.begin() + k);
								--num_refs;
								--k;
							}
						}
					}
					// 法線の正規化
					XMStoreFloat3(&v.normal, XMVector3Normalize(n1));
				}
			}
		}

		/// @brief UV座標の処理
		/// @param m メッシュデータの参照
		void process_uvs(mesh& m)
		{
			utl::vector<vertex> old_vertices;
			old_vertices.swap(m.vertices);
			utl::vector<u32> old_indices(m.indices.size());
			old_indices.swap(m.indices);

			const u32 num_vertices{ (u32)old_vertices.size() };
			const u32 num_indices{ (u32)old_indices.size() };
			assert(num_indices && num_vertices);

			// インデックスの参照を生成
			utl::vector<utl::vector<u32>> idx_ref(num_vertices);	// 頂点参照インデックス
			for (u32 i{ 0 }; i < num_indices; ++i)
				idx_ref[old_indices[i]].emplace_back(i);

			// UV座標の再計算
			for (u32 i{ 0 }; i < num_vertices; ++i)
			{
				// インデックス参照
				auto& refs{ idx_ref[i] };
				u32 num_refs{ (u32)refs.size() };
				for (u32 j{ 0 }; j < num_refs; ++j)
				{
					// インデックスの削除
					m.indices[refs[j]] = (u32)m.vertices.size();
					vertex& v{ old_vertices[old_indices[refs[j]]] };
					v.uv = m.uv_sets[0][refs[j]];
					m.vertices.emplace_back(v);

					// 同じUV座標の頂点を削除
					for (u32 k{ j + 1 }; k < num_refs; ++k)
					{
						v2& uv1{ m.uv_sets[0][refs[k]] };
						if (XMScalarNearEqual(v.uv.x, uv1.x, epsilon) && XMScalarNearEqual(v.uv.y, uv1.y, epsilon))
						{
							m.indices[refs[k]] = m.indices[refs[j]];
							refs.erase(refs.begin() + k);
							--num_refs;
							--k;
						}
					}
				}
			}
		}

		/// @brief 頂点のパック
		/// @param m メッシュデータの参照
		void pack_vertices_static(mesh& m)
		{
			const u32 num_vertices{ (u32)m.vertices.size() };
			assert(num_vertices);
			m.packed_vertices_static.reserve(num_vertices);
			for (u32 i{ 0 }; i < num_vertices; ++i)
			{
				vertex& v{ m.vertices[i] };
				const u8 signs{ (u8)((v.normal.z > 0.0f) << 1) };
				const u16 normal_x{ (u16)pack_float<16>(v.normal.x,-1.0f,1.0f) };
				const u16 normal_y{ (u16)pack_float<16>(v.normal.y,-1.0f,1.0f) };
				// TODO: 符号とx/y成分で接線を詰める

				// 静的頂点の追加
				m.packed_vertices_static.emplace_back(packed_vertex::vertex_static{ v.position, {0,0,0}, signs, {normal_x,normal_y}, {}, v.uv });
			}
		}

		/// @brief 頂点の処理
		/// @param m メッシュデータの参照
		/// @param settings インポート設定
		void process_vertices(mesh& m, const geometry_import_settings& settings)
		{
			// インデックス数が3の倍数であることを確認
			assert((m.raw_indices.size() % 3) == 0);

			// 頂点数が0でないことを確認
			if (settings.calculate_normals || m.normals.empty())
			{
				// 法線の再計算
				recalculate_normals(m);
			}

			// 法線の処理
			process_normals(m, settings.smoothings_angle);

			// UV座標の再計算
			if (!m.uv_sets.empty())
			{
				process_uvs(m);
			}

			// 静的頂点のパック
			pack_vertices_static(m);
		}

		/// @brief メッシュのサイズを取得
		/// @param m メッシュデータの参照
		/// @return メッシュのサイズ
		u64 get_mesh_size(const mesh& m)
		{
			const u64 num_vertices{ m.vertices.size() };
			const u64 vertex_buffer_size{ sizeof(packed_vertex::vertex_static) * num_vertices };
			const u64 index_size{ (num_vertices < (1 << 16)) ? sizeof(u16) : sizeof(u32) };
			const u64 index_buffer_size{ index_size * m.indices.size() };
			constexpr u64 su32{ sizeof(u32) };
			const u64 size
			{
				su32 + m.name.size() +	// メッシュ名の長さとメッシュ名文字列のスペース
				su32 +					// LODID
				su32 +					// 頂点サイズ
				su32 +					// 頂点数
				su32 + 					// インデックスサイズ（16ビットまたは32ビット）
				su32 +					// インデックス数
				sizeof(f32) +			// LOD閾値
				vertex_buffer_size +	// 頂点のスペース
				index_buffer_size		// インデックスのスペース
			};
			return size;
		}

		/// @brief メッシュのサイズを取得
		/// @param scene シーンデータの参照
		/// @return メッシュのサイズ
		u64 get_scene_size(const scene& scene)
		{
			constexpr u64 su32{ sizeof(u32) };
			u64 size
			{
				su32 +				// 名前の長さ
				scene.name.size() +	// 名前の文字列のスペース
				su32				// LOD数
			};

			for (auto& lod : scene.lod_groups)
			{
				u64 lod_size
				{
					su32 + lod.name.size() + // LOD名の長さとLPD名文字列のスペース
					su32					 // このLODのメッシュ数
				};

				for (auto& m : lod.meshes)
				{
					lod_size += get_mesh_size(m);
				}

				size += lod_size;
			}
			return size;
		}

		/// @brief メッシュデータのパック
		/// @param m メッシュデータの参照
		/// @param buffer バッファ
		/// @param at バッファの位置
		void pack_mesh_data(const mesh& m, u8* const buffer, u64& at)
		{
			constexpr u64 su32{ sizeof(u32) };
			u32 s{ 0 };	// サイズ

			// メッシュ名のコピー
			s = (u32)m.name.size();						// メッシュ名の長さ
			memcpy(&buffer[at], &s, su32); at += su32;	// メッシュ名の長さのコピーして位置を進める
			memcpy(&buffer[at], m.name.c_str(), s); at += s;	// メッシュ名のコピーして位置を進める

			// LODIDのコピー
			s = m.lod_id;								// LODID
			memcpy(&buffer[at], &s, su32); at += su32;	// LODIDのコピーして位置を進める

			// 頂点サイズのコピー
			constexpr u32 vertex_size{ sizeof(packed_vertex::vertex_static) };
			s = vertex_size;							// 頂点サイズ
			memcpy(&buffer[at], &s, su32); at += su32;	// 頂点サイズのコピーして位置を進める

			// 頂点数のコピー
			const u32 num_vertices{ (u32)m.vertices.size() };
			s = num_vertices;							// 頂点数
			memcpy(&buffer[at], &s, su32); at += su32;	// 頂点数のコピーして位置を進める

			// インデックスサイズのコピー
			const u32 index_size{ (num_vertices < (1 << 16)) ? sizeof(u16) : sizeof(u32) };
			s = index_size;								// インデックスサイズ
			memcpy(&buffer[at], &s, su32); at += su32;	// インデックスサイズのコピーして位置を進める

			// インデックス数のコピー
			const u32 num_indices{ (u32)m.indices.size() };
			s = num_indices;							// インデックス数
			memcpy(&buffer[at], &s, su32); at += su32;	// インデックス数のコピーして位置を進める

			// LOD閾値のコピー
			memcpy(&buffer[at], &m.lod_threshold, sizeof(f32)); at += sizeof(f32);	// LOD閾値のコピーして位置を進める

			// 頂点のコピー
			s = vertex_size * num_vertices;				// 頂点のサイズ
			memcpy(&buffer[at], m.packed_vertices_static.data(), s); at += s;	// 頂点のコピーして位置を進める

			// インデックスのコピー
			s = index_size * num_indices;				// インデックスのサイズ
			void* data{ (void*)m.indices.data() };
			utl::vector<u16> indices;

			if (index_size == sizeof(u16))
			{
				indices.resize(num_indices);
				for (u32 i{ 0 }; i < num_indices; ++i)
					indices[i] = (u16)m.indices[i];
				data = (void*)indices.data();
			}
			memcpy(&buffer[at], data, s); at += s;		// インデックスのコピーして位置を進める
		}

	} // 匿名名前空間

	/// @brief シーンデータの処理
	/// @param scene シーンデータの参照
	/// @param settings インポート設定
	void process_scene(scene& scene, const geometry_import_settings& settings)
	{
		for (auto& lod : scene.lod_groups)
		{
			for (auto& m : lod.meshes)
			{
				process_vertices(m, settings);
			}
		}
	}

	/// @brief シーンデータのパック
	/// @param scene シーンデータの参照
	/// @param data シーンデータ
	void pack_data(const scene& scene, scene_data& data)
	{
		constexpr u64 su32{ sizeof(u32) };
		const u64 scene_size{ get_scene_size(scene) };
		data.buffer_size = (u32)scene_size;
		data.buffer = (u8*)CoTaskMemAlloc(scene_size);
		assert(data.buffer);

		u8* const buffer{ data.buffer };						// バッファ
		u64 at{ 0 };											// バッファの位置
		u32 s{ 0 };												// サイズ

		// シーン名のコピー
		s = (u32)scene.name.size();								// 名前の長さ
		memcpy(&buffer[at], &s, su32); at += su32;				// 名前の長さのコピーして位置を進める
		memcpy(&buffer[at], scene.name.c_str(), s); at += s;	// 名前のコピーして位置を進める

		// LOD数のコピー
		s = (u32)scene.lod_groups.size();						// LOD数
		memcpy(&buffer[at], &s, su32); at += su32;				// LOD数のコピーして位置を進める

		for (auto& lod : scene.lod_groups)
		{
			// LOD名のコピー
			s = (u32)lod.name.size();							// LOD名の長さ
			memcpy(&buffer[at], &s, su32); at += su32;			// LOD名の長さのコピーして位置を進める
			memcpy(&buffer[at], lod.name.c_str(), s); at += s;	// LOD名のコピーして位置を進める

			// このLODのメッシュ数
			s = (u32)lod.meshes.size();							// このLODのメッシュ数
			memcpy(&buffer[at], &s, su32); at += su32;			// このLODのメッシュ数のコピーして位置を進める

			for (auto& m : lod.meshes)
			{
				pack_mesh_data(m, buffer, at);
			}
		}
		assert(scene_size == at);
	}

}	// namespace dxforge::tools
