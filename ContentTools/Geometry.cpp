// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Geometry.cpp]
// 作成日 : 2024/12/26
// 作成者 : 田中ミノル
// 概要 :
//
// 更新履歴
// 2024/12/26 新規作成
// 2025/01/12 メッシュデータのパック処理の変更
// 2025/01/12 IOStream.hのblob_stream_readerを使用するように変更
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Geometry.h"
#include "../DXForgeEngine/Utilities/IOStream.h"

namespace dxforge::tools
{
	namespace
	{
		using namespace math;		// 数学関数
		using namespace DirectX;	// DirectX関数

		void calculate_tangents(mesh& m)
		{
			// 輸入タンジェントを使用しない
			m.tangents.clear();

			const u32 num_indices{ (u32)m.raw_indices.size() };
			utl::vector<XMVECTOR> tangents(num_indices, XMVectorZero());
			utl::vector<XMVECTOR> bitangents(num_indices, XMVectorZero());
			utl::vector<XMVECTOR> positions(num_indices);

			for (u32 i{ 0 }; i < num_indices; ++i)
			{
				positions[i] = XMLoadFloat3(&m.vertices[m.indices[i]].position);
			}

			for (u32 i{ 0 }; i < num_indices; i += 3)
			{
				const u32 i0{ i + 0 };
				const u32 i1{ i + 1 };
				const u32 i2{ i + 2 };

				const XMVECTOR& p0{ positions[i0] };
				const XMVECTOR& p1{ positions[i1] };
				const XMVECTOR& p2{ positions[i2] };

				const math::v2& uv0{ m.vertices[m.indices[i0]].uv };
				const math::v2& uv1{ m.vertices[m.indices[i1]].uv };
				const math::v2& uv2{ m.vertices[m.indices[i2]].uv };

				const math::v2 duv1{ uv1.x - uv0.x, uv1.y - uv0.y };
				const math::v2 duv2{ uv2.x - uv0.x, uv2.y - uv0.y };

				const XMVECTOR dp1{ p1 - p0 };
				const XMVECTOR dp2{ p2 - p0 };

				f32 det{ duv1.x * duv2.y - duv1.y * duv2.x };
				if (abs(det) < math::epsilon) det = math::epsilon;

				const f32 inv_det{ 1.f / det };
				const XMVECTOR t{ (dp1 * duv2.y - dp2 * duv1.y) * inv_det };
				const XMVECTOR b{ (dp2 * duv1.x - dp1 * duv2.x) * inv_det };

				tangents[i0] += t;
				tangents[i1] += t;
				tangents[i2] += t;
				bitangents[i0] += b;
				bitangents[i1] += b;
				bitangents[i2] += b;
			}

			for (u32 i{ 0 }; i < num_indices; ++i)
			{
				const XMVECTOR& t{ tangents[i] };
				const XMVECTOR& b{ bitangents[i] };
				const XMVECTOR& n{ XMLoadFloat3(&m.vertices[m.indices[i]].normal) };

				math::v3 tangent;
				XMStoreFloat3(&tangent, XMVector3Normalize(t - n * XMVector3Dot(n, t)));
				f32 handedness;
				XMStoreFloat(&handedness, XMVector3Dot(XMVector3Cross(t, b), n));

				handedness = handedness > 0.f ? 1.f : -1.f;

				m.vertices[m.indices[i]].tangent = { tangent.x, tangent.y, tangent.z, handedness };
			}
		}

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
				utl::vector<u32>& refs{ idx_ref[i] };
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
				utl::vector<u32>& refs{ idx_ref[i] };
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

		/// @brief 頂点のサイズを取得
		/// @param elements_type 要素タイプ
		/// @return 頂点のサイズ
		u64 get_vertex_element_size(elements::elements_type::type elements_type)
		{
			using namespace elements;
			switch (elements_type)
			{
			case elements_type::static_color:					return sizeof(static_color);
			case elements_type::static_normal:					return sizeof(static_normal);
			case elements_type::static_normal_texture:			return sizeof(static_normal_texture);
			case elements_type::skeletal:						return sizeof(skeletal);
			case elements_type::skeletal_color:					return sizeof(skeletal_color);
			case elements_type::skeletal_normal:				return sizeof(skeletal_normal);
			case elements_type::skeletal_normal_color:			return sizeof(skeletal_normal_color);
			case elements_type::skeletal_normal_texture:		return sizeof(skeletal_normal_texture);
			case elements_type::skeletal_normal_texture_color:	return sizeof(skeletal_normal_texture_color);
			}
			return 0;
		}

		/// @brief 頂点のパック
		/// @param m メッシュデータの参照
		void pack_vertices(mesh& m)
		{
			const u32 num_vertices{ (u32)m.vertices.size() };
			assert(num_vertices);

			m.position_buffer.resize(num_vertices * sizeof(v3));
			v3* const position_buffer{ (v3* const)m.position_buffer.data() };

			for (u32 i{ 0 }; i < num_vertices; ++i)
			{
				position_buffer[i] = m.vertices[i].position;
			}

			struct  u16v2 { u16 x, y; };
			struct u16v3 { u16 x, y, z; };

			utl::vector<u8> t_signs(num_vertices);
			utl::vector<u16v2> normals(num_vertices);
			utl::vector<u16v2> tangents(num_vertices);
			utl::vector<u16v3> joint_weights(num_vertices);

			if (m.elements_type & elements::elements_type::static_normal)
			{
				// normal only
				for (u32 i{ 0 }; i < num_vertices; ++i)
				{
					vertex& v{ m.vertices[i] };
					t_signs[i] = (u8)((v.normal.z > 0.0f) << 1);
					normals[i] = { (u16)pack_float<16>(v.normal.x, -1.0f, 1.0f), (u16)pack_float<16>(v.normal.y, -1.0f, 1.0f) };
				}

				if (m.elements_type & elements::elements_type::static_normal_texture)
				{
					// full T-space
					for (u32 i{ 0 }; i < num_vertices; ++i)
					{
						vertex& v{ m.vertices[i] };
						t_signs[i] |= (u8)((v.tangent.w > 0.f) | ((v.tangent.z > 0.f) << 1));
						tangents[i] = { (u16)math::pack_float<16>(v.tangent.x, -1.f, 1.f), (u16)math::pack_float<16>(v.tangent.y, -1.f, 1.f) };
					}
				}
			}

			if (m.elements_type & elements::elements_type::skeletal)
			{
				for (u32 i{ 0 }; i < num_vertices; ++i)
				{
					vertex& v{ m.vertices[i] };
					// pack joint weights (from [0.0,1.0] to [0...255])
					joint_weights[i] =
					{
						(u8)pack_unit_float<8>(v.joint_weights.x),
						(u8)pack_unit_float<8>(v.joint_weights.y),
						(u8)pack_unit_float<8>(v.joint_weights.z)
					};
					// NOTE: w3はシェーダーで計算
				}
			}

			m.element_buffer.resize(get_vertex_element_size(m.elements_type) * num_vertices);
			using namespace elements;

			switch (m.elements_type)
			{
			case elements_type::static_color:
			{
				static_color* const element_buffer{ (static_color* const)m.element_buffer.data() };
				for (u32 i{ 0 }; i < num_vertices; ++i)
				{
					vertex& v{ m.vertices[i] };
					element_buffer[i] = { {v.red,v.green,v.blue},{} };
				}
			}
			break;
			case elements_type::static_normal:
			{
				static_normal* const element_buffer{ (static_normal* const)m.element_buffer.data() };
				for (u32 i{ 0 }; i < num_vertices; ++i)
				{
					vertex& v{ m.vertices[i] };
					element_buffer[i] = { {v.red,v.green,v.blue},t_signs[i],{normals[i].x,normals[i].y} };
				}
			}
			break;
			case elements_type::static_normal_texture:
			{
				static_normal_texture* const element_buffer{ (static_normal_texture* const)m.element_buffer.data() };
				for (u32 i{ 0 }; i < num_vertices; ++i)
				{
					vertex& v{ m.vertices[i] };
					element_buffer[i] = { {v.red,v.green,v.blue},t_signs[i],
										{normals[i].x,normals[i].y},{tangents[i].x,tangents[i].y},
										v.uv };
				}
			}
			break;
			case elements_type::skeletal:
			{
				skeletal* const element_buffer{ (skeletal* const)m.element_buffer.data() };
				for (u32 i{ 0 }; i < num_vertices; ++i)
				{
					vertex& v{ m.vertices[i] };
					const u16 indices[4]{ (u16)v.joint_indices.x,(u16)v.joint_indices.y,(u16)v.joint_indices.z,(u16)v.joint_indices.w };
					element_buffer[i] = { {(u8)joint_weights[i].x,(u8)joint_weights[i].y,(u8)joint_weights[i].z},{},
						{indices[0],indices[1],indices[2],indices[3]} };
				}
			}
			break;
			case elements_type::skeletal_color:
			{
				skeletal_color* const element_buffer{ (skeletal_color* const)m.element_buffer.data() };
				for (u32 i{ 0 }; i < num_vertices; ++i)
				{
					vertex& v{ m.vertices[i] };
					const u16 indices[4]{ (u16)v.joint_indices.x,(u16)v.joint_indices.y,(u16)v.joint_indices.z,(u16)v.joint_indices.w };
					element_buffer[i] = { {(u8)joint_weights[i].x,(u8)joint_weights[i].y,(u8)joint_weights[i].z},{},
						{indices[0],indices[1],indices[2],indices[3]},
						{v.red,v.green,v.blue},{} };
				}
			}
			break;
			case elements_type::skeletal_normal:
			{
				skeletal_normal* const element_buffer{ (skeletal_normal* const)m.element_buffer.data() };
				for (u32 i{ 0 }; i < num_vertices; ++i)
				{
					vertex& v{ m.vertices[i] };
					const u16 indices[4]{ (u16)v.joint_indices.x,(u16)v.joint_indices.y,(u16)v.joint_indices.z,(u16)v.joint_indices.w };
					element_buffer[i] = { {(u8)joint_weights[i].x,(u8)joint_weights[i].y,(u8)joint_weights[i].z},t_signs[i],
						{indices[0],indices[1],indices[2],indices[3]},
						{normals[i].x,normals[i].y} };
				}
			}
			break;
			case elements_type::skeletal_normal_color:
			{
				skeletal_normal_color* const element_buffer{ (skeletal_normal_color* const)m.element_buffer.data() };
				for (u32 i{ 0 }; i < num_vertices; ++i)
				{
					vertex& v{ m.vertices[i] };
					const u16 indices[4]{ (u16)v.joint_indices.x,(u16)v.joint_indices.y,(u16)v.joint_indices.z,(u16)v.joint_indices.w };
					element_buffer[i] = { {(u8)joint_weights[i].x,(u8)joint_weights[i].y,(u8)joint_weights[i].z},t_signs[i],
						{indices[0],indices[1],indices[2],indices[3]},
						{normals[i].x,normals[i].y},{v.red,v.green,v.blue},{} };
				}
			}
			break;
			case elements_type::skeletal_normal_texture:
			{
				skeletal_normal_texture* const element_buffer{ (skeletal_normal_texture* const)m.element_buffer.data() };
				for (u32 i{ 0 }; i < num_vertices; ++i)
				{
					vertex& v{ m.vertices[i] };
					const u16 indices[4]{ (u16)v.joint_indices.x,(u16)v.joint_indices.y,(u16)v.joint_indices.z,(u16)v.joint_indices.w };
					element_buffer[i] = { {(u8)joint_weights[i].x,(u8)joint_weights[i].y,(u8)joint_weights[i].z},t_signs[i],
						{indices[0],indices[1],indices[2],indices[3]},
						{normals[i].x,normals[i].y}, {tangents[i].x, tangents[i].y}, v.uv };
				}
			}
			break;
			case elements_type::skeletal_normal_texture_color:
			{
				skeletal_normal_texture_color* const element_buffer{ (skeletal_normal_texture_color* const)m.element_buffer.data() };
				for (u32 i{ 0 }; i < num_vertices; ++i)
				{
					vertex& v{ m.vertices[i] };
					const u16 indices[4]{ (u16)v.joint_indices.x, (u16)v.joint_indices.y, (u16)v.joint_indices.z, (u16)v.joint_indices.w };
					element_buffer[i] = { {(u8)joint_weights[i].x, (u8)joint_weights[i].y, (u8)joint_weights[i].z}, t_signs[i],
						{indices[0], indices[1], indices[2], indices[3]},
						{normals[i].x, normals[i].y}, {tangents[i].x, tangents[i].y}, v.uv,
						{v.red, v.green, v.blue}, {} };
				}
			}
			break;
			}

		}

		elements::elements_type::type determine_elements_type(const mesh& m)
		{
			using namespace elements;
			elements_type::type type{};

			if (m.normals.size())
			{
				if (m.uv_sets.size() && m.uv_sets[0].size())
				{
					type = elements_type::static_normal_texture;
				}
				else
				{
					type = elements_type::static_normal;
				}
			}
			else if (m.colors.size())
			{
				type = elements_type::static_color;
			}

			// TODO: 骨格メッシュのデータがない。 骨格メッシュについては後で拡張する。
			return type;
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

			if ((settings.calculate_tangents || m.tangents.empty()) && !m.uv_sets.empty())
			{
				calculate_mikk_tspace(m);
				//calculate_tangents(m);
			}

			// NOTE: m.tangentsには、インポートされた接線ベクトルの値が格納されます。
			//		タンジェントが計算された場合は空になります。
			//		したがって、process_tangentsは、ソースファイルからタンジェントがインポートされたときにのみ呼び出されます。
			if (!m.tangents.empty())
			{
				process_tangents(m);
			}

			// 頂点のパック
			m.elements_type = determine_elements_type(m);
			pack_vertices(m);
		}

		/// @brief メッシュのサイズを取得
		/// @param m メッシュデータの参照
		/// @return メッシュのサイズ
		u64 get_mesh_size(const mesh& m)
		{
			const u64 num_vertices{ m.vertices.size() };
			const u64 position_buffer_size{ m.position_buffer.size() };
			assert(position_buffer_size == sizeof(v3) * num_vertices);
			const u64 element_buffer_size{ m.element_buffer.size() };
			assert(element_buffer_size == get_vertex_element_size(m.elements_type) * num_vertices);
			const u64 index_size{ (num_vertices < (1 << 16)) ? sizeof(u16) : sizeof(u32) };
			const u64 index_buffer_size{ index_size * m.indices.size() };
			constexpr u64 su32{ sizeof(u32) };
			const u64 size
			{
				su32 + m.name.size() +	// メッシュ名の長さとメッシュ名文字列のスペース
				su32 +					// LODID
				su32 +					// 頂点要素サイズ（位置要素を除いた頂点サイズ）
				su32 +					// 要素タイプ列挙
				su32 +					// 頂点数
				su32 + 					// インデックスサイズ（16ビットまたは32ビット）
				su32 +					// インデックス数
				sizeof(f32) +			// LOD閾値
				position_buffer_size +	// 頂点位置のスペース
				element_buffer_size +	// 頂点要素のスペース
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

			for (const auto& lod : scene.lod_groups)
			{
				u64 lod_size
				{
					su32 + lod.name.size() + // LOD名の長さとLPD名文字列のスペース
					su32					 // このLODのメッシュ数
				};

				for (const auto& m : lod.meshes)
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
		void pack_mesh_data(const mesh& m, utl::blob_stream_writer& blob)
		{
			// メッシュ名の書き込み
			blob.write((u32)m.name.size());
			blob.write(m.name.c_str(), m.name.size());

			// LOD IDの書き込み
			blob.write(m.lod_id);

			// 頂点要素サイズの書き込み
			const u32 elements_size{ (u32)get_vertex_element_size(m.elements_type) };
			blob.write(elements_size);

			// 要素タイプの書き込み
			blob.write((u32)m.elements_type);

			// 頂点数の書き込み
			const u32 num_vertices{ (u32)m.vertices.size() };
			blob.write(num_vertices);

			// インデックスサイズ(16ビットまたは32ビット)の書き込み
			const u32 index_size{ (num_vertices < (1 << 16)) ? sizeof(u16) : sizeof(u32) };
			blob.write(index_size);

			// インデックス数の書き込み
			const u32 num_indices{ (u32)m.indices.size() };
			blob.write(num_indices);

			// LOD閾値の書き込み
			blob.write(m.lod_threshold);

			// position bufferの書き込み
			assert(m.position_buffer.size() == sizeof(math::v3) * num_vertices);
			blob.write(m.position_buffer.data(), m.position_buffer.size());

			// element bufferの書き込み
			assert(m.element_buffer.size() == elements_size * num_vertices);
			blob.write(m.element_buffer.data(), m.element_buffer.size());

			// index dataの書き込み
			const u32 index_buffer_size{ index_size * num_indices };
			const u8* data{ (const u8*)m.indices.data() };
			utl::vector<u16> indices;

			if (index_size == sizeof(u16))
			{
				indices.resize(num_indices);
				for (u32 i{ 0 }; i < num_indices; ++i) indices[i] = (u16)m.indices[i];
				data = (const u8*)indices.data();
			}
			blob.write(data, index_buffer_size);
		}

		bool split_meshes_by_material(u32 material_idx, const mesh& m, mesh& submesh)
		{
			submesh.name = m.name;
			submesh.lod_threshold = m.lod_threshold;
			submesh.lod_id = m.lod_id;
			submesh.material_used.emplace_back(material_idx);
			submesh.uv_sets.resize(m.uv_sets.size());

			const u32 num_polys{ (u32)m.raw_indices.size() / 3 };
			utl::vector<u32> vertex_ref(m.positions.size(), u32_invalid_id);

			for (u32 i{ 0 }; i < num_polys; ++i)
			{
				const u32 mtl_idx{ m.material_indices[i] };
				if (mtl_idx != material_idx) continue;

				const u32 index{ i * 3 };
				for (u32 j = index; j < index + 3; ++j)
				{
					const u32 v_idx{ m.raw_indices[j] };
					if (vertex_ref[v_idx] != u32_invalid_id)
					{
						submesh.raw_indices.emplace_back(vertex_ref[v_idx]);
					}
					else
					{
						submesh.raw_indices.emplace_back((u32)submesh.positions.size());
						vertex_ref[v_idx] = (u32)submesh.raw_indices.back();
						submesh.positions.emplace_back(m.positions[v_idx]);
					}

					if (m.normals.size())
					{
						submesh.normals.emplace_back(m.normals[j]);
					}

					if (m.tangents.size())
					{
						submesh.tangents.emplace_back(m.tangents[j]);
					}

					for (u32 k{ 0 }; k < m.uv_sets.size(); ++k)
					{
						if (m.uv_sets[k].size())
						{
							submesh.uv_sets[k].emplace_back(m.uv_sets[k][j]);
						}
					}

				}
			}
			assert((submesh.raw_indices.size() % 3) == 0);
			return !submesh.positions.empty();
		}

		void split_meshes_by_material(scene& scene, progression* const progression)
		{
			assert(progression);
			progression->callback(0, 0);

			for (auto& lod : scene.lod_groups)
			{
				utl::vector<mesh> new_meshes;

				for (const auto& m : lod.meshes)
				{
					// このメッシュに複数のマテリアルが使用されている場合は、サブメッシュに分割します。
					const u32 num_materials{ (u32)m.material_used.size() };
					if (num_materials > 1)
					{
						for (u32 i{ 0 }; i < num_materials; ++i)
						{
							mesh submesh{};
							if (split_meshes_by_material(m.material_used[i], m, submesh))
							{
								new_meshes.emplace_back(submesh);
							}
						}
					}
					else
					{
						new_meshes.emplace_back(m);
					}
				}

				progression->callback(progression->value(), progression->max_value() + (u32)new_meshes.size());
				new_meshes.swap(lod.meshes);
			}
		}

		/// @brief ベクトルに追加
		/// @tparam T 要素の型
		/// @param dst 追加先のベクトル
		/// @param src 追加するベクトル
		template <typename T> void append_to_vector_pod(utl::vector<T>& dst, const utl::vector<T>& src)
		{
			if (src.empty()) return;
			const u32 num_elements{ (u32)dst.size() };
			dst.resize(dst.size() + src.size());
			memcpy(&dst[num_elements], src.data(), src.size() * sizeof(T));
		}

	} // 匿名名前空間

	/// @brief シーンデータの処理
	/// @param scene シーンデータの参照
	/// @param settings インポート設定
	void process_scene(scene& scene, const geometry_import_settings& settings, progression* const progression)
	{
		assert(progression);
		split_meshes_by_material(scene, progression);

		for (auto& lod : scene.lod_groups)
		{
			for (auto& m : lod.meshes)
			{
				process_vertices(m, settings);
				progression->callback(progression->value() + 1, progression->max_value());
			}
		}
	}

	/// @brief シーンデータのパック
	/// @param scene シーンデータの参照
	/// @param data シーンデータ
	void pack_data(const scene& scene, scene_data& data)
	{
		const u64 scene_size{ get_scene_size(scene) };
		data.buffer_size = (u32)scene_size;
		data.buffer = (u8*)CoTaskMemAlloc(scene_size);
		assert(data.buffer);

		utl::blob_stream_writer blob{ data.buffer, data.buffer_size };

		// シーン名を書き込む
		blob.write((u32)scene.name.size());
		blob.write(scene.name.c_str(), scene.name.size());

		// LOD数を書き込む
		blob.write((u32)scene.lod_groups.size());

		for (const auto& lod : scene.lod_groups)
		{
			// LOD名を書き込む
			blob.write((u32)lod.name.size());
			blob.write(lod.name.c_str(), lod.name.size());
			// このLODのメッシュ数を書き込む
			blob.write((u32)lod.meshes.size());

			for (const auto& m : lod.meshes)
			{
				pack_mesh_data(m, blob);
			}
		}

		assert(scene_size == blob.offset());
	}

	/// @brief メッシュの結合
	/// @param lod LODグループ
	/// @param combined_mesh 結合されたメッシュへの参照
	/// @param progression 進行状況
	/// @return 成功したかどうか
	bool coalesce_meshes(const lod_group& lod, mesh& combined_mesh, progression* const progression)
	{
		assert(lod.meshes.size());
		const mesh& first_mesh{ lod.meshes[0] };
		combined_mesh.name = first_mesh.name;
		combined_mesh.elements_type = determine_elements_type(first_mesh);
		combined_mesh.lod_threshold = first_mesh.lod_threshold;
		combined_mesh.lod_id = first_mesh.lod_id;
		combined_mesh.uv_sets.resize(first_mesh.uv_sets.size());

		for (u32 mesh_idx{ 0 }; mesh_idx < lod.meshes.size(); ++mesh_idx)
		{
			const mesh& m{ lod.meshes[mesh_idx] };

			// メッシュの要素が一致しない場合は、結合メッシュをクリアしてfalseを返す
			if (combined_mesh.elements_type != determine_elements_type(m) || combined_mesh.uv_sets.size() != m.uv_sets.size() || combined_mesh.lod_id != m.lod_id || !math::is_equal(combined_mesh.lod_threshold, m.lod_threshold))
			{
				combined_mesh = {};
				return false;
			}
		}

		for (u32 mesh_idx{ 0 }; mesh_idx < lod.meshes.size(); ++mesh_idx)
		{
			const mesh& m{ lod.meshes[mesh_idx] };

			const u32 position_count{ (u32)combined_mesh.positions.size() };
			const u32 raw_index_base{ (u32)combined_mesh.raw_indices.size() };

			append_to_vector_pod(combined_mesh.positions, m.positions);
			append_to_vector_pod(combined_mesh.normals, m.normals);
			append_to_vector_pod(combined_mesh.tangents, m.tangents);
			append_to_vector_pod(combined_mesh.colors, m.colors);

			for (u32 i{ 0 }; i < combined_mesh.uv_sets.size(); ++i)
			{
				append_to_vector_pod(combined_mesh.uv_sets[i], m.uv_sets[i]);
			}

			append_to_vector_pod(combined_mesh.material_indices, m.material_indices);
			append_to_vector_pod(combined_mesh.raw_indices, m.raw_indices);

			for (u32 i{ raw_index_base }; i < combined_mesh.raw_indices.size(); ++i)
			{
				combined_mesh.raw_indices[i] += position_count;
			}

			progression->callback(progression->value(), progression->max_value() > 1 ? progression->max_value() - 1 : 1);
		}

		for (const u32 mtl_idx : combined_mesh.material_indices)
		{
			if (std::find(combined_mesh.material_used.begin(), combined_mesh.material_used.end(), mtl_idx) == combined_mesh.material_used.end())
			{
				combined_mesh.material_used.emplace_back(mtl_idx);
			}
		}

		return true;
	}
}	// namespace dxforge::tools
