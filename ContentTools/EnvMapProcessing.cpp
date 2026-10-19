// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [EnvMapProcessing.cpp]
// 作成日 : 2026/10/18
// 作成者 : 田中ミノル
// 概要 :
// 環境マップ(正距円筒のHDR画像 -> キューブマップ)の変換処理
// 更新履歴
// 2026/10/18 新規作成
// 2026/10/18 機能レベル11_0のデバイスでもUAVの解除が正しく行われるよう修正
// 2026/10/18 CPU版のキューブマップ変換のサンプリング位置をGPU版と同じ画素の中心に修正(キューブ面と正距円筒画像の両方で半画素ずれていた)
// 2026/10/18 拡散光のIBL用にキューブマップを事前フィルタする処理(irradiance map)のGPU版とCPU版を追加 (Primal Day 073)
// 2026/10/19 拡散光の事前フィルタの入力が大きいときは縮小してから処理し、処理量に上限を設けた
// 2026/10/19 鏡面反射のIBL用にキューブマップをラフネスごとのミップへ事前フィルタする処理のGPU版とCPU版を追加 (Primal Day 074)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "EnvMapProcessing.h"
#include <dxgi1_6.h>
#include <algorithm>
#include <atomic>
#include <thread>

using namespace DirectX;
using namespace Microsoft::WRL;

namespace dxforge::tools
{
	namespace
	{
		namespace shaders
		{
			// CompileShaders.cmdがビルド前に生成するシェーダのバイトコード(x64/Compiled)
#include "EnvMapProcessing_EquirectangularToCubeMapCS.inc"
#include "EnvMapProcessing_PrefilterDiffuseEnvMapCS.inc"
#include "EnvMapProcessing_PrefilterSpecularEnvMapCS.inc"
		}

		constexpr u32 prefiltered_diffuse_cubemap_size{ 64 };
		constexpr u32 prefiltered_specular_cubemap_size{ 256 };
		// 鏡面反射のIBLのミップの数。ミップmのラフネスはm / (roughness_mip_levels - 1)で、最後のミップがラフネス1。
		constexpr u32 roughness_mip_levels{ 6 };
		// 鏡面反射の事前フィルタで、出力の1画素あたりに使うGGXの重点サンプリングのサンプル数(Primalと同じ)。
		constexpr u32 prefilter_specular_sample_count{ 1024 };
		// EnvMapProcessing.hlslのSAMPLE_OFFSETと同じ値にすること(画素の中心からサンプリングする)。
		constexpr f32 sample_offset{ 0.5f };

		// EnvMapProcessing.hlslのcbuffer Constantsと同じレイアウトにすること。
		struct ShaderConstants
		{
			u32 CubeMapInSize;
			u32 CubeMapOutSize;
			u32 SampleCount;
			f32 Roughness;
		};

		/// @brief キューブマップの面とその面上の座標からサンプリング方向を求める
		/// @param face 面のインデックス(0: X+, 1: X-, 2: Y+, 3: Y-, 4: Z+, 5: Z-)
		/// @param u 面上の横方向の座標[-1, 1]
		/// @param v 面上の縦方向の座標[-1, 1]
		/// @return 正規化されたサンプリング方向
		math::v3 get_sample_direction_equirectangular(u32 face, f32 u, f32 v)
		{
			math::v3 directions[6]{
				{ -u,    1.f, -v },     // X+ Left
				{  u,   -1.f, -v },     // X- Right
				{  v,    u,    1.f },   // Y+ Bottom
				{ -v,    u,   -1.f },   // Y- Top
				{  1.f,  u,   -v },     // Z+ Front
				{ -1.f, -u,   -v },     // Z- Back
			};

			XMVECTOR dir{ XMLoadFloat3(&directions[face]) };
			dir = XMVector3Normalize(dir);
			math::v3 normalized_dir;
			XMStoreFloat3(&normalized_dir, dir);

			return normalized_dir;
		}

		/// @brief キューブマップの面とその面上の座標からサンプリング方向を求める(D3Dのキューブマップの面の配置)
		/// @details EnvMapProcessing.hlslのGetSampleDirectionCubemapと同じにすること。
		///          GPU版はこの方向で入力のキューブマップをサンプリングするので、画素の中心の方向ならその画素の値そのものが返る。
		/// @param face 面のインデックス(0: X+, 1: X-, 2: Y+, 3: Y-, 4: Z+, 5: Z-)
		/// @param u 面上の横方向の座標[-1, 1]
		/// @param v 面上の縦方向の座標[-1, 1]
		/// @return 正規化されたサンプリング方向
		math::v3 get_sample_direction_cubemap(u32 face, f32 u, f32 v)
		{
			math::v3 directions[6]{
				{  1.f, -v,   -u },     // X+ Left
				{ -1.f, -v,    u },     // X- Right
				{  u,    1.f,  v },     // Y+ Bottom
				{  u,   -1.f, -v },     // Y- Top
				{  u,   -v,    1.f },   // Z+ Front
				{ -u,   -v,   -1.f },   // Z- Back
			};

			XMVECTOR dir{ XMLoadFloat3(&directions[face]) };
			dir = XMVector3Normalize(dir);
			math::v3 normalized_dir;
			XMStoreFloat3(&normalized_dir, dir);

			return normalized_dir;
		}

		/// @brief 方向ベクトルを正距円筒画像のUV座標に変換する
		/// @param dir 正規化された方向ベクトル
		/// @return UV座標[0, 1]
		math::v2 direction_to_equirectangular(const math::v3& dir)
		{
			const f32 phi{ atan2f(dir.y, dir.x) };
			const f32 theta{ XMScalarACos(dir.z) };
			const f32 s{ phi * math::inv_two_pi + 0.5f };
			const f32 t{ theta * math::inv_pi };

			return { s, t };
		}

		/// @brief 正距円筒画像からキューブマップの1面をサンプリングする(CPU版)
		/// @param env_map 正距円筒画像(DXGI_FORMAT_R32G32B32A32_FLOAT)
		/// @param cube_face 出力するキューブマップの面(DXGI_FORMAT_R32G32B32A32_FLOAT)
		/// @param face_index 面のインデックス
		/// @param mirror 左右反転するかどうか
		void sample_cube_face(const Image& env_map, const Image& cube_face, u32 face_index, bool mirror)
		{
			assert(cube_face.width == cube_face.height);
			const f32 inv_width{ 1.f / (f32)cube_face.width };
			const f32 inv_height{ 1.f / (f32)cube_face.height };
			const u32 row_pitch{ (u32)cube_face.rowPitch };
			const u32 env_max_x{ (u32)env_map.width - 1 };
			const u32 env_max_y{ (u32)env_map.height - 1 };
			const u32 env_row_pitch{ (u32)env_map.rowPitch };
			// env_mapとcube_faceはどちらもDXGI_FORMAT_R32G32B32A32_FLOATであることを前提とする。
			constexpr u32 bytes_per_pixel{ sizeof(f32) * 4 };

			for (u32 y{ 0 }; y < cube_face.height; ++y)
			{
				const f32 v{ ((y + sample_offset) * inv_height) * 2.f - 1.f };

				for (u32 x{ 0 }; x < cube_face.width; ++x)
				{
					const f32 u{ ((x + sample_offset) * inv_width) * 2.f - 1.f };

					const math::v3 sample_direction{ get_sample_direction_equirectangular(face_index, u, v) };
					math::v2 uv{ direction_to_equirectangular(sample_direction) };
					assert(uv.x >= 0.f && uv.x <= 1.f && uv.y >= 0.f && uv.y <= 1.f);

					if (mirror) uv.x = 1.f - uv.x;
					// GPU版(SampleLevel)と同じく、テクセルiがUVの[i / width, (i + 1) / width)を覆うとみなして最近傍のテクセルを選ぶ。
					// uvが1のときは端のテクセルにクランプする(GPU版のサンプラーもCLAMP)。
					const u32 pos_x{ std::min((u32)(uv.x * env_map.width), env_max_x) };
					const u32 pos_y{ std::min((u32)(uv.y * env_map.height), env_max_y) };
					u8* dst_pixel{ &cube_face.pixels[row_pitch * y + x * bytes_per_pixel] };

					u8* const src_pixel{ &env_map.pixels[env_row_pitch * pos_y + pos_x * bytes_per_pixel] };
					memcpy(dst_pixel, src_pixel, bytes_per_pixel);
				}
			}
		}

		/// @brief キューブマップの1画素の中心の方向と、その画素の立体角に比例する重み
		struct cubemap_texel
		{
			math::v3 direction;
			f32 solid_angle;
		};

		/// @brief 1面がsize x sizeのキューブマップの全画素について、画素の中心の方向と立体角に比例する重みを求める
		/// @details EnvMapProcessing.hlslのSampleHemisphereBruteと同じ式を使う。並びは[面][y][x]。
		/// @param size キューブマップの1面のサイズ
		/// @return 6 * size * size個の画素
		utl::vector<cubemap_texel> get_cubemap_texels(u32 size)
		{
			utl::vector<cubemap_texel> texels(6ull * size * size);
			const f32 inv_size{ 1.f / (f32)size };
			u32 index{ 0 };

			for (u32 face{ 0 }; face < 6; ++face)
				for (u32 y{ 0 }; y < size; ++y)
				{
					const f32 v{ ((y + sample_offset) * inv_size) * 2.f - 1.f };

					for (u32 x{ 0 }; x < size; ++x)
					{
						const f32 u{ ((x + sample_offset) * inv_size) * 2.f - 1.f };
						const f32 tmp{ 1.f + u * u + v * v };

						cubemap_texel& texel{ texels[index++] };
						texel.direction = get_sample_direction_cubemap(face, u, v);
						texel.solid_angle = 4.f / (sqrtf(tmp) * tmp);
					}
				}

			return texels;
		}

		/// @brief キューブマップの1面を拡散光のIBL用に事前フィルタする(CPU版)
		/// 入力の面がこのサイズより大きければ、総当たりのフィルタの前に縮小する。
		/// 処理量は(出力の画素数) x (入力の画素数)なので、縮小しないと例えば1024の6面の入力で約1.5e11回のループになり、
		/// GPU版はTDRにかかる恐れがある。放射照度は低周波なので、出力と同じサイズまで縮小しても結果はほとんど変わらない。
		constexpr u32 prefilter_diffuse_max_input_size{ prefiltered_diffuse_cubemap_size };

		/// 入力の面がprefilter_diffuse_max_input_sizeより大きければ、各面のミップ0を縮小したキューブマップ
		/// (DXGI_FORMAT_R32G32B32A32_FLOAT、ミップ1つ)をresizedに作る。大きくなければresizedは空のまま。
		/// CPU版とGPU版が同じ入力を使うように、どちらもこの関数を通す。
		HRESULT limit_prefilter_diffuse_input(const ScratchImage& cubemaps, ScratchImage& resized)
		{
			const TexMetadata& metadata{ cubemaps.GetMetadata() };
			if (metadata.width <= prefilter_diffuse_max_input_size)
			{
				return S_OK;
			}

			utl::vector<Image> faces{};
			for (u32 i{ 0 }; i < (u32)metadata.arraySize; ++i)
			{
				const Image* const image{ cubemaps.GetImage(0, i, 0) };
				assert(image);
				faces.emplace_back(*image);
			}

			TexMetadata mip0_metadata{ metadata };
			mip0_metadata.mipLevels = 1;

			// 面積で重み付ける縮小。WICは使わない(32ビット浮動小数点のまま処理し、COMの初期化も要らない)。
			constexpr TEX_FILTER_FLAGS filter{ TEX_FILTER_TRIANGLE | TEX_FILTER_FORCE_NON_WIC };
			constexpr u32 size{ prefilter_diffuse_max_input_size };
			if (metadata.format == DXGI_FORMAT_R32G32B32A32_FLOAT)
			{
				return Resize(faces.data(), faces.size(), mip0_metadata, size, size, filter, resized);
			}

			ScratchImage f32_faces{};
			const HRESULT hr{ Convert(faces.data(), faces.size(), mip0_metadata, DXGI_FORMAT_R32G32B32A32_FLOAT,
									  TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, f32_faces) };
			if (FAILED(hr))
			{
				return hr;
			}

			return Resize(f32_faces.GetImages(), f32_faces.GetImageCount(), f32_faces.GetMetadata(), size, size, filter, resized);
		}

		/// @param texels 入力のキューブマップの全画素の方向と重み(get_cubemap_texels())
		/// @param src_faces 入力のキューブマップのミップ0の6面(DXGI_FORMAT_R32G32B32A32_FLOAT)
		/// @param dst_face 出力するキューブマップの面(DXGI_FORMAT_R32G32B32A32_FLOAT)
		/// @param face_index 出力する面のインデックス
		void prefilter_diffuse_face(const utl::vector<cubemap_texel>& texels, const Image* const* src_faces, const Image& dst_face, u32 face_index)
		{
			assert(dst_face.width == dst_face.height);
			const u32 src_size{ (u32)src_faces[0]->width };
			const f32 inv_size{ 1.f / (f32)dst_face.width };
			constexpr u32 channel_count{ 4 };

			for (u32 y{ 0 }; y < dst_face.height; ++y)
			{
				const f32 v{ ((y + sample_offset) * inv_size) * 2.f - 1.f };
				f32* const dst_row{ (f32*)(dst_face.pixels + dst_face.rowPitch * y) };

				for (u32 x{ 0 }; x < dst_face.width; ++x)
				{
					const f32 u{ ((x + sample_offset) * inv_size) * 2.f - 1.f };
					const math::v3 normal{ get_sample_direction_cubemap(face_index, u, v) };

					// 画素数が多いので、和は倍精度で求める。
					double irradiance[3]{};
					double weight_sum{ 0.0 };
					const cubemap_texel* texel{ texels.data() };

					for (u32 face{ 0 }; face < 6; ++face)
					{
						const Image& src_face{ *src_faces[face] };
						for (u32 ty{ 0 }; ty < src_size; ++ty)
						{
							const f32* const src_row{ (const f32*)(src_face.pixels + src_face.rowPitch * ty) };
							for (u32 tx{ 0 }; tx < src_size; ++tx, ++texel)
							{
								const math::v3& dir{ texel->direction };
								const f32 cos_theta{ dir.x * normal.x + dir.y * normal.y + dir.z * normal.z };
								if (cos_theta > 0.f)
								{
									const double weight{ (double)cos_theta * texel->solid_angle };
									const f32* const radiance{ &src_row[tx * channel_count] };
									irradiance[0] += radiance[0] * weight;
									irradiance[1] += radiance[1] * weight;
									irradiance[2] += radiance[2] * weight;
									weight_sum += weight;
								}
							}
						}
					}

					f32* const dst{ &dst_row[x * channel_count] };
					dst[0] = (f32)(irradiance[0] / weight_sum);
					dst[1] = (f32)(irradiance[1] / weight_sum);
					dst[2] = (f32)(irradiance[2] / weight_sum);
					dst[3] = 1.f;
				}
			}
		}

		/// @brief 鏡面反射の事前フィルタのミップのラフネス
		/// @details NOTE: PrimalはRoughness = mip / roughness_mip_levelsとしていて最後のミップが5/6になるが、
		///          Primal Day 075のシェーダはPerceptualRoughness * 5のミップをサンプリングするので、ミップ5がラフネス1になるようにする。
		constexpr f32 roughness_of_mip(u32 mip)
		{
			return (f32)mip / (f32)(roughness_mip_levels - 1);
		}

		/// @brief Van der Corputの基数2の根基逆関数(ビットの反転)。EnvMapProcessing.hlslのRadicalInverse_VdCと同じ。
		f32 radical_inverse_vdc(u32 bits)
		{
			bits = (bits << 16u) | (bits >> 16u);
			bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
			bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
			bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
			bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
			return (f32)bits * 2.3283064365386963e-10f; // / 0x100000000
		}

		/// @brief GGXの重点サンプリングの1サンプル(EnvMapProcessing.hlslのPrefilterEnvMapの1回のループ)のうち、出力の画素によらない部分
		struct specular_sample
		{
			math::v3 half_vector;	// 接空間のハーフベクトル(z軸が法線)
			f32 mip_level;			// サンプリングする入力のミップ(小数。トライリニア)
		};

		/// @brief EnvMapProcessing.hlslのPrefilterEnvMapと同じ式で、すべてのサンプルのハーフベクトルとミップを求める
		/// @details N == Vなので、NoHは接空間のハーフベクトルのzで、ミップは出力の画素によらない。
		/// @param roughness ラフネス
		/// @param sample_count サンプル数
		/// @param input_size 入力のキューブマップの1面のサイズ
		/// @return サンプル
		utl::vector<specular_sample> get_specular_samples(f32 roughness, u32 sample_count, u32 input_size)
		{
			utl::vector<specular_sample> samples(sample_count);
			const f32 r2{ roughness * roughness };
			const f32 a4{ r2 * r2 };
			const f32 inv_omega_p{ (6.f * (f32)input_size * (f32)input_size) / (4.f * math::pi) };

			for (u32 i{ 0 }; i < sample_count; ++i)
			{
				// ImportanceSampleGGX(Hammersley(i, sample_count), a4)
				const f32 phi{ 2.f * math::pi * ((f32)i / (f32)sample_count) };
				const f32 e_y{ radical_inverse_vdc(i) };
				const f32 cos_theta{ sqrtf((1.f - e_y) / (1.f + (a4 - 1.f) * e_y)) };
				const f32 sin_theta{ sqrtf(std::max(1.f - cos_theta * cos_theta, 0.f)) };

				specular_sample& sample{ samples[i] };
				sample.half_vector = { sin_theta * cosf(phi), sin_theta * sinf(phi), cos_theta };

				// D_GGX(NoH, a4) / 4 が出射方向Lの確率密度。
				const f32 no_h{ std::min(cos_theta, 1.f) };
				const f32 d{ (no_h * a4 - no_h) * no_h + 1.f };
				const f32 pdf{ a4 / (math::pi * d * d) * 0.25f };
				const f32 omega_s{ 1.f / ((f32)sample_count * pdf + 0.0001f) };
				sample.mip_level = roughness == 0.f ? 0.f : 0.5f * log2f(omega_s * inv_omega_p);
			}

			return samples;
		}

		/// @brief 方向をD3Dのキューブマップの面と面上の座標に変換する(get_sample_direction_cubemap()の逆)
		/// @param dir 方向(正規化されていなくてよい)
		/// @param face 面のインデックス
		/// @param s 面上の横方向の座標[0, 1]
		/// @param t 面上の縦方向の座標[0, 1]
		void direction_to_cubemap(const math::v3& dir, u32& face, f32& s, f32& t)
		{
			const f32 ax{ fabsf(dir.x) };
			const f32 ay{ fabsf(dir.y) };
			const f32 az{ fabsf(dir.z) };
			f32 ma, sc, tc;

			if (ax >= ay && ax >= az)
			{
				face = dir.x >= 0.f ? 0 : 1;
				ma = ax;
				sc = dir.x >= 0.f ? -dir.z : dir.z;
				tc = -dir.y;
			}
			else if (ay >= az)
			{
				face = dir.y >= 0.f ? 2 : 3;
				ma = ay;
				sc = dir.x;
				tc = dir.y >= 0.f ? dir.z : -dir.z;
			}
			else
			{
				face = dir.z >= 0.f ? 4 : 5;
				ma = az;
				sc = dir.z >= 0.f ? dir.x : -dir.x;
				tc = -dir.y;
			}

			s = (sc / ma + 1.f) * 0.5f;
			t = (tc / ma + 1.f) * 0.5f;
		}

		/// @brief 鏡面反射の事前フィルタの入力(32ビット浮動小数点の、すべてのミップを持つキューブマップ)をサンプリングする(CPU版)
		struct specular_input
		{
			const ScratchImage& cubemaps;
			u32 mip_levels;

			/// @brief 面faceのミップmipの画素(x, y)。x, yが面の外(-1かsize)なら、その画素の中心の方向にある隣の面の画素を返す。
			///        x, yの両方が面の外(面の角)ならnullptrを返す。
			const f32* texel(u32 cubemap, u32 mip, u32 face, s32 x, s32 y) const
			{
				const Image& image{ *cubemaps.GetImage(mip, cubemap * 6 + face, 0) };
				const s32 size{ (s32)image.width };
				const bool x_out{ x < 0 || x >= size };
				const bool y_out{ y < 0 || y >= size };
				if (x_out && y_out) return nullptr;

				if (x_out || y_out)
				{
					// 面の平面を延長した座標で画素の中心の方向を求め、その方向にある隣の面の画素を使う(継ぎ目のないキューブマップのフィルタ)。
					const f32 inv_size{ 1.f / (f32)size };
					const f32 u{ ((x + sample_offset) * inv_size) * 2.f - 1.f };
					const f32 v{ ((y + sample_offset) * inv_size) * 2.f - 1.f };
					f32 s, t;
					direction_to_cubemap(get_sample_direction_cubemap(face, u, v), face, s, t);
					x = std::clamp((s32)(s * size), 0, size - 1);
					y = std::clamp((s32)(t * size), 0, size - 1);
					return texel(cubemap, mip, face, x, y);
				}

				return (const f32*)(image.pixels + image.rowPitch * y) + x * 4;
			}

			/// @brief 1つのミップをバイリニアでサンプリングする
			void sample_bilinear(u32 cubemap, u32 mip, u32 face, f32 s, f32 t, f32* rgb) const
			{
				const f32 size{ (f32)cubemaps.GetImage(mip, cubemap * 6 + face, 0)->width };
				const f32 px{ s * size - sample_offset };
				const f32 py{ t * size - sample_offset };
				const f32 fx0{ floorf(px) };
				const f32 fy0{ floorf(py) };
				const f32 wx{ px - fx0 };
				const f32 wy{ py - fy0 };
				const s32 x0{ (s32)fx0 };
				const s32 y0{ (s32)fy0 };

				const f32* texels[4]{
					texel(cubemap, mip, face, x0, y0), texel(cubemap, mip, face, x0 + 1, y0),
					texel(cubemap, mip, face, x0, y0 + 1), texel(cubemap, mip, face, x0 + 1, y0 + 1),
				};
				const f32 weights[4]{ (1.f - wx) * (1.f - wy), wx * (1.f - wy), (1.f - wx) * wy, wx * wy };

				// 面の角では4つ目の画素がないので、ほかの3つの平均で代える(D3D11の仕様と同じ)。
				f32 corner[3]{};
				for (u32 i{ 0 }; i < 4; ++i)
				{
					if (texels[i]) continue;
					for (u32 j{ 0 }; j < 4; ++j)
					{
						if (j == i) continue;
						assert(texels[j]);
						for (u32 c{ 0 }; c < 3; ++c) corner[c] += texels[j][c] * (1.f / 3.f);
					}
					texels[i] = corner;
				}

				for (u32 c{ 0 }; c < 3; ++c)
				{
					rgb[c] = texels[0][c] * weights[0] + texels[1][c] * weights[1] + texels[2][c] * weights[2] + texels[3][c] * weights[3];
				}
			}

			/// @brief 方向dirを、ミップmip_level(小数)でトライリニアにサンプリングする(GPU版のSampleLevelと同じ)
			void sample(u32 cubemap, const math::v3& dir, f32 mip_level, f32* rgb) const
			{
				u32 face;
				f32 s, t;
				direction_to_cubemap(dir, face, s, t);

				mip_level = std::clamp(mip_level, 0.f, (f32)(mip_levels - 1));
				const u32 mip0{ (u32)mip_level };
				const f32 w{ mip_level - (f32)mip0 };
				sample_bilinear(cubemap, mip0, face, s, t, rgb);

				if (w > 0.f && mip0 + 1 < mip_levels)
				{
					f32 rgb1[3];
					sample_bilinear(cubemap, mip0 + 1, face, s, t, rgb1);
					for (u32 c{ 0 }; c < 3; ++c) rgb[c] += (rgb1[c] - rgb[c]) * w;
				}
			}
		};

		/// @brief 鏡面反射の事前フィルタの入力をそろえる(CPU版とGPU版で同じ入力を使うように、どちらもこの関数を通す)
		/// @details 各キューブマップのミップ0を32ビット浮動小数点に変換し、1面がprefiltered_specular_cubemap_sizeでなければ面積で重み付けて
		///          縮小(または拡大)し、すべてのミップを作る。入力が大きくても、事前フィルタの処理量はこのサイズで決まる(TDR対策)。
		/// @param cubemaps 入力のキューブマップ
		/// @param input 出力先(DXGI_FORMAT_R32G32B32A32_FLOAT、1面がprefiltered_specular_cubemap_size、すべてのミップ)
		/// @return 結果
		HRESULT prepare_prefilter_specular_input(const ScratchImage& cubemaps, ScratchImage& input)
		{
			const TexMetadata& metadata{ cubemaps.GetMetadata() };

			utl::vector<Image> faces{};
			for (u32 i{ 0 }; i < (u32)metadata.arraySize; ++i)
			{
				const Image* const image{ cubemaps.GetImage(0, i, 0) };
				assert(image);
				faces.emplace_back(*image);
			}

			TexMetadata mip0_metadata{ metadata };
			mip0_metadata.mipLevels = 1;

			const Image* images{ faces.data() };
			const TexMetadata* images_metadata{ &mip0_metadata };
			HRESULT hr{ S_OK };

			ScratchImage f32_faces{};
			if (metadata.format != DXGI_FORMAT_R32G32B32A32_FLOAT)
			{
				hr = Convert(images, faces.size(), *images_metadata, DXGI_FORMAT_R32G32B32A32_FLOAT,
							 TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, f32_faces);
				if (FAILED(hr))
				{
					return hr;
				}

				images = f32_faces.GetImages();
				images_metadata = &f32_faces.GetMetadata();
			}

			// WICは使わない(32ビット浮動小数点のまま処理し、COMの初期化も要らない)。
			ScratchImage resized{};
			constexpr u32 size{ prefiltered_specular_cubemap_size };
			if (metadata.width != size)
			{
				hr = Resize(images, faces.size(), *images_metadata, size, size, TEX_FILTER_TRIANGLE | TEX_FILTER_FORCE_NON_WIC, resized);
				if (FAILED(hr))
				{
					return hr;
				}

				images = resized.GetImages();
				images_metadata = &resized.GetMetadata();
			}

			return GenerateMipMaps(images, faces.size(), *images_metadata, TEX_FILTER_BOX | TEX_FILTER_FORCE_NON_WIC, 0, input);
		}

		/// @brief 鏡面反射の事前フィルタの出力(DXGI_FORMAT_R32G32B32A32_FLOAT)のミップ0(ラフネス0)に、入力のミップ0をコピーする
		/// @details ラフネス0ではGGXの分布が反射方向だけになり、フィルタしても入力と変わらないので、フィルタせずにコピーする(Primalと同じ)。
		/// @param input prepare_prefilter_specular_input()でそろえた入力
		/// @param prefiltered 出力
		void copy_roughness_zero_mip(const ScratchImage& input, const ScratchImage& prefiltered)
		{
			const u32 array_size{ (u32)input.GetMetadata().arraySize };
			assert(array_size == prefiltered.GetMetadata().arraySize);
			for (u32 i{ 0 }; i < array_size; ++i)
			{
				const Image& src{ *input.GetImage(0, i, 0) };
				const Image& dst{ *prefiltered.GetImage(0, i, 0) };
				assert(src.slicePitch == dst.slicePitch);
				memcpy(dst.pixels, src.pixels, src.slicePitch);
			}
		}

		/// @brief 鏡面反射のIBLの1画素を求める(CPU版, EnvMapProcessing.hlslのPrefilterEnvMapと同じ和)
		/// @param input 入力
		/// @param cubemap 入力のキューブマップのインデックス
		/// @param samples get_specular_samples()で求めたサンプル
		/// @param normal 出力の画素の中心の方向(N = V = R)
		/// @param rgb 出力
		void prefilter_specular_texel(const specular_input& input, u32 cubemap, const utl::vector<specular_sample>& samples,
									  const math::v3& normal, f32* rgb)
		{
			// GetTangentFrame(N)
			const math::v3 up{ fabsf(normal.z) < 0.999f ? math::v3{ 0.f, 0.f, 1.f } : math::v3{ 1.f, 0.f, 0.f } };
			const XMVECTOR n{ XMLoadFloat3(&normal) };
			const XMVECTOR tangent_x{ XMVector3Normalize(XMVector3Cross(XMLoadFloat3(&up), n)) };
			const XMVECTOR tangent_y{ XMVector3Cross(n, tangent_x) };
			math::v3 tx, ty;
			XMStoreFloat3(&tx, tangent_x);
			XMStoreFloat3(&ty, tangent_y);

			double color[3]{};
			double total_weight{ 0.0 };

			for (const specular_sample& sample : samples)
			{
				// H = mul(ImportanceSampleGGX(Xi, a4), tangentFrame), L = 2 * dot(V, H) * H - V
				const math::v3& h_t{ sample.half_vector };
				const math::v3 h{
					h_t.x * tx.x + h_t.y * ty.x + h_t.z * normal.x,
					h_t.x * tx.y + h_t.y * ty.y + h_t.z * normal.y,
					h_t.x * tx.z + h_t.y * ty.z + h_t.z * normal.z,
				};
				const f32 v_dot_h{ normal.x * h.x + normal.y * h.y + normal.z * h.z };
				const math::v3 l{ 2.f * v_dot_h * h.x - normal.x, 2.f * v_dot_h * h.y - normal.y, 2.f * v_dot_h * h.z - normal.z };
				const f32 n_dot_l{ std::clamp(normal.x * l.x + normal.y * l.y + normal.z * l.z, 0.f, 1.f) };

				if (n_dot_l > 0.f)
				{
					f32 radiance[3];
					input.sample(cubemap, l, sample.mip_level, radiance);
					for (u32 c{ 0 }; c < 3; ++c) color[c] += (double)radiance[c] * n_dot_l;
					total_weight += n_dot_l;
				}
			}

			// i = 0のサンプルはH = N(L = N)なので、total_weightは0にならない。
			for (u32 c{ 0 }; c < 3; ++c) rgb[c] = (f32)(color[c] / total_weight);
		}

		/// @brief func(0) ~ func(count - 1)を、CPUのコアの数のスレッドで並列に実行する
		/// @param count 回数
		/// @param func 実行する処理(引数はインデックス)
		template<typename F> void parallel_for(u32 count, F func)
		{
			std::atomic<u32> next{ 0 };
			auto worker{ [&] { for (u32 i{ next++ }; i < count; i = next++) func(i); } };

			const u32 thread_count{ std::max(std::thread::hardware_concurrency(), 1u) - 1 };
			utl::vector<std::thread> threads{};
			for (u32 i{ 0 }; i < thread_count; ++i) threads.emplace_back(worker);
			worker();
			for (std::thread& thread : threads) thread.join();
		}

		/// @brief 32ビット浮動小数点の結果を、入力のキューブマップのフォーマットに変換して出力する
		/// @param working_scratch 結果(DXGI_FORMAT_R32G32B32A32_FLOAT)
		/// @param format 出力のフォーマット
		/// @param prefiltered 出力先
		/// @return 結果
		HRESULT convert_prefiltered_output(ScratchImage& working_scratch, DXGI_FORMAT format, ScratchImage& prefiltered)
		{
			if (format == DXGI_FORMAT_R32G32B32A32_FLOAT)
			{
				prefiltered = std::move(working_scratch);
				return S_OK;
			}

			return Convert(working_scratch.GetImages(), working_scratch.GetImageCount(), working_scratch.GetMetadata(), format,
						   TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, prefiltered);
		}

		/// @brief コンピュートシェーダにバインドされているリソースをすべて解除する
		/// @param ctx デバイスコンテキスト
		void reset_d3d11_context(ID3D11DeviceContext* ctx)
		{
			u8 zero_mem_block[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT * sizeof(void*)];
			memset(&zero_mem_block, 0, sizeof(zero_mem_block));

			// NOTE: UAVのスロット数は機能レベル11_1では64、11_0では8。上限を超えて指定すると呼び出しが無視される。
			ComPtr<ID3D11Device> device{};
			ctx->GetDevice(&device);
			const u32 uav_slot_count{ device && device->GetFeatureLevel() >= D3D_FEATURE_LEVEL_11_1
				? (u32)D3D11_1_UAV_SLOT_COUNT : (u32)D3D11_PS_CS_UAV_REGISTER_COUNT };
			ctx->CSSetUnorderedAccessViews(0, uav_slot_count, (ID3D11UnorderedAccessView**)&zero_mem_block[0], nullptr);
			ctx->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, (ID3D11ShaderResourceView**)&zero_mem_block[0]);
			ctx->CSSetConstantBuffers(0, D3D11_COMMONSHADER_CONSTANT_BUFFER_API_SLOT_COUNT, (ID3D11Buffer**)&zero_mem_block[0]);
		}

		/// @brief 定数バッファに値を書き込む
		/// @param ctx デバイスコンテキスト
		/// @param constant_buffer 定数バッファ
		/// @param constants 書き込む値
		/// @return 結果
		HRESULT set_constants(ID3D11DeviceContext* ctx, ID3D11Buffer* constant_buffer, ShaderConstants constants)
		{
			D3D11_MAPPED_SUBRESOURCE mapped_buffer{};
			HRESULT hr{ ctx->Map(constant_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped_buffer) };
			if (FAILED(hr) || !mapped_buffer.pData)
			{
				return hr;
			}

			memcpy(mapped_buffer.pData, &constants, sizeof(ShaderConstants));
			ctx->Unmap(constant_buffer, 0);
			return hr;
		}

		/// @brief 定数バッファを作成する
		/// @param device デバイス
		/// @param constant_buffer 作成した定数バッファ
		/// @return 結果
		HRESULT create_constant_buffer(ID3D11Device* device, ID3D11Buffer** constant_buffer)
		{
			D3D11_BUFFER_DESC desc{};
			desc.ByteWidth = sizeof(ShaderConstants);
			desc.Usage = D3D11_USAGE_DYNAMIC;
			desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
			desc.MiscFlags = 0;
			desc.StructureByteStride = 0;

			HRESULT hr{ device->CreateBuffer(&desc, nullptr, constant_buffer) };
			return hr;
		}

		/// @brief リニアサンプラーを作成する
		/// @param device デバイス
		/// @param linear_sampler 作成したサンプラー
		/// @return 結果
		HRESULT create_linear_sampler(ID3D11Device* device, ID3D11SamplerState** linear_sampler)
		{
			D3D11_SAMPLER_DESC desc{};
			desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
			desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
			desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
			desc.MinLOD = 0;
			desc.MaxLOD = D3D11_FLOAT32_MAX;

			return device->CreateSamplerState(&desc, linear_sampler);
		}

		/// @brief 2Dテクスチャ配列のUAVを作成する
		/// @param device デバイス
		/// @param format フォーマット
		/// @param array_size 配列のサイズ
		/// @param first_array_slice 最初の配列スライス
		/// @param mip_slice ミップスライス
		/// @param texture テクスチャ
		/// @param texture_uav 作成したUAV
		/// @return 結果
		HRESULT create_texture_2d_uav(ID3D11Device* device, DXGI_FORMAT format, u32 array_size, u32 first_array_slice,
									  u32 mip_slice, ID3D11Resource* texture, ID3D11UnorderedAccessView** texture_uav)
		{
			D3D11_UNORDERED_ACCESS_VIEW_DESC desc{};
			desc.Format = format;
			desc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE2DARRAY;
			desc.Texture2DArray.ArraySize = array_size;
			desc.Texture2DArray.FirstArraySlice = first_array_slice;
			desc.Texture2DArray.MipSlice = mip_slice;

			return device->CreateUnorderedAccessView(texture, &desc, texture_uav);
		}

		/// @brief GPUのテクスチャをCPUにコピーしてScratchImageに格納する
		/// @param ctx デバイスコンテキスト
		/// @param width 幅
		/// @param height 高さ
		/// @param array_size 配列のサイズ
		/// @param mip_levels ミップレベル数
		/// @param format フォーマット
		/// @param is_cubemap キューブマップかどうか
		/// @param gpu_resource GPUのテクスチャ
		/// @param cpu_resource CPUから読み取り可能なステージングテクスチャ
		/// @param result 出力先
		/// @return 結果
		HRESULT download_texture_2d(ID3D11DeviceContext* ctx, u32 width, u32 height, u32 array_size, u32 mip_levels, DXGI_FORMAT format, bool is_cubemap,
									ID3D11Texture2D* gpu_resource, ID3D11Texture2D* cpu_resource, ScratchImage& result)
		{
			ctx->CopyResource(cpu_resource, gpu_resource);

			HRESULT hr{ is_cubemap ?
				result.InitializeCube(format, width, height, array_size / 6, mip_levels) :
				result.Initialize2D(format, width, height, array_size, mip_levels)
			};

			if (FAILED(hr))
			{
				return hr;
			}

			for (u32 img_idx{ 0 }; img_idx < array_size; ++img_idx)
			{
				for (u32 mip{ 0 }; mip < mip_levels; ++mip)
				{
					D3D11_MAPPED_SUBRESOURCE mapped_resource{};
					const u32 resource_idx{ mip + (img_idx * mip_levels) };
					hr = ctx->Map(cpu_resource, resource_idx, D3D11_MAP_READ, 0, &mapped_resource);
					if (FAILED(hr))
					{
						result.Release();
						return hr;
					}

					const Image& img{ *result.GetImage(mip, img_idx, 0) };
					u8* src{ (u8*)mapped_resource.pData };
					u8* dst{ img.pixels };

					for (u32 row{ 0 }; row < img.height; ++row)
					{
						memcpy(dst, src, img.rowPitch);
						src += mapped_resource.RowPitch;
						dst += img.rowPitch;
					}

					ctx->Unmap(cpu_resource, resource_idx);
				}
			}

			return hr;
		}

		/// @brief コンピュートシェーダをディスパッチする
		void dispatch(ID3D11DeviceContext* ctx,
					  ID3D11ShaderResourceView* const* const srv_array, ID3D11UnorderedAccessView* const* const uav_array,
					  ID3D11Buffer* const* const buffers_array, ID3D11SamplerState* const* const samplers_array,
					  ID3D11ComputeShader* shader, math::u32v3 group_count)
		{
			ctx->CSSetShaderResources(0, 1, srv_array);
			ctx->CSSetUnorderedAccessViews(0, 1, uav_array, nullptr);
			ctx->CSSetConstantBuffers(0, 1, &buffers_array[0]);
			ctx->CSSetSamplers(0, 1, &samplers_array[0]);
			ctx->CSSetShader(shader, nullptr, 0);
			ctx->Dispatch(group_count.x, group_count.y, group_count.z);
		}

	} // 匿名名前空間

	HRESULT equirectangular_to_cubemap(const Image* env_maps, u32 env_map_count, u32 cubemap_size,
									   bool use_prefilter_size, bool mirror_cubemap, ScratchImage& cube_maps)
	{
		if (use_prefilter_size)
		{
			cubemap_size = prefiltered_specular_cubemap_size;
		}

		assert(env_maps && env_map_count);
		HRESULT hr{ S_OK };

		// 画像1枚につき1つのキューブマップを作成する。
		ScratchImage working_scratch{};
		hr = working_scratch.InitializeCube(DXGI_FORMAT_R32G32B32A32_FLOAT, cubemap_size, cubemap_size, env_map_count, 1);
		if (FAILED(hr))
		{
			return hr;
		}

		for (u32 i{ 0 }; i < env_map_count; ++i)
		{
			const Image& env_map{ env_maps[i] };
			assert(math::is_equal((f32)env_map.width / (f32)env_map.height, 2.f));

			// NOTE: すべての環境マップは同じサイズとフォーマットの正距円筒画像である。
			//       (サイズとフォーマットの一致はインポート関数ですでに確認済み)
			//       サンプリングしやすいように、各環境マップを32ビット浮動小数点のリニアな色空間に変換する。
			const Image* env_map_image{ &env_map };
			ScratchImage f32_env_map{};
			if (env_map.format != DXGI_FORMAT_R32G32B32A32_FLOAT)
			{
				hr = Convert(env_map, DXGI_FORMAT_R32G32B32A32_FLOAT, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, f32_env_map);
				if (FAILED(hr))
				{
					return hr;
				}

				assert(f32_env_map.GetImageCount() == 1);
				env_map_image = f32_env_map.GetImages();
			}

			const Image* dst_images{ &working_scratch.GetImages()[i * 6] };
			const bool mirror{ mirror_cubemap };

			std::thread threads[]{
				std::thread{ [&] { sample_cube_face(*env_map_image, dst_images[0], 0, mirror); } },
				std::thread{ [&] { sample_cube_face(*env_map_image, dst_images[1], 1, mirror); } },
				std::thread{ [&] { sample_cube_face(*env_map_image, dst_images[2], 2, mirror); } },
				std::thread{ [&] { sample_cube_face(*env_map_image, dst_images[3], 3, mirror); } },
				std::thread{ [&] { sample_cube_face(*env_map_image, dst_images[4], 4, mirror); } },
			};

			sample_cube_face(*env_map_image, dst_images[5], 5, mirror);

			for (u32 face{ 0 }; face < 5; ++face) threads[face].join();
		}

		if (env_maps[0].format != DXGI_FORMAT_R32G32B32A32_FLOAT)
		{
			hr = Convert(working_scratch.GetImages(), working_scratch.GetImageCount(), working_scratch.GetMetadata(), env_maps[0].format,
						 TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, cube_maps);
		}
		else
		{
			cube_maps = std::move(working_scratch);
		}

		return hr;
	}

	HRESULT equirectangular_to_cubemap(ID3D11Device* device, const Image* env_maps, u32 env_map_count, u32 cubemap_size,
									   bool use_prefilter_size, bool mirror_cubemap, ScratchImage& cubemaps_out)
	{
		if (use_prefilter_size)
		{
			cubemap_size = prefiltered_specular_cubemap_size;
		}

		assert(env_maps && env_map_count);
		const DXGI_FORMAT format{ env_maps[0].format };
		const u32 array_size{ env_map_count * 6 };
		ComPtr<ID3D11DeviceContext> ctx{};
		device->GetImmediateContext(ctx.GetAddressOf());
		assert(ctx.Get());

		HRESULT hr{ S_OK };

		// 出力先のリソースを作成
		ComPtr<ID3D11Texture2D> cubemaps{};
		ComPtr<ID3D11Texture2D> cubemaps_cpu{};
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = cubemap_size;
			desc.MipLevels = 1;
			desc.ArraySize = array_size;
			desc.Format = format;
			desc.SampleDesc = { 1, 0 };
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			desc.CPUAccessFlags = 0;
			desc.MiscFlags = 0;

			hr = device->CreateTexture2D(&desc, nullptr, cubemaps.GetAddressOf());
			if (FAILED(hr))
			{
				return hr;
			}

			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

			hr = device->CreateTexture2D(&desc, nullptr, cubemaps_cpu.GetAddressOf());
			if (FAILED(hr))
			{
				return hr;
			}
		}

		ComPtr<ID3D11ComputeShader> shader{};
		hr = device->CreateComputeShader(shaders::EnvMapProcessing_EquirectangularToCubeMapCS,
										 sizeof(shaders::EnvMapProcessing_EquirectangularToCubeMapCS),
										 nullptr, shader.GetAddressOf());
		if (FAILED(hr))
		{
			return hr;
		}

		ComPtr<ID3D11Buffer> constant_buffer{};
		{
			hr = create_constant_buffer(device, constant_buffer.GetAddressOf());
			if (FAILED(hr))
			{
				return hr;
			}

			ShaderConstants constants{};
			constants.CubeMapOutSize = cubemap_size;
			// SampleCountをキューブマップの左右反転の切り替えとして流用している。
			constants.SampleCount = mirror_cubemap ? 1 : 0;
			hr = set_constants(ctx.Get(), constant_buffer.Get(), constants);
			if (FAILED(hr))
			{
				return hr;
			}
		}

		ComPtr<ID3D11SamplerState> linear_sampler{};
		hr = create_linear_sampler(device, linear_sampler.GetAddressOf());
		if (FAILED(hr))
		{
			return hr;
		}

		reset_d3d11_context(ctx.Get());

		for (u32 i{ 0 }; i < env_map_count; ++i)
		{
			ComPtr<ID3D11UnorderedAccessView> cubemap_uav{};
			hr = create_texture_2d_uav(device, format, 6, i * 6, 0, cubemaps.Get(), cubemap_uav.ReleaseAndGetAddressOf());
			if (FAILED(hr))
			{
				return hr;
			}

			const Image& src{ env_maps[i] };

			// 元画像をGPUにアップロード
			ComPtr<ID3D11Texture2D> env_map{};
			{
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = (u32)src.width;
				desc.Height = (u32)src.height;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.Format = format;
				desc.SampleDesc = { 1, 0 };
				desc.Usage = D3D11_USAGE_IMMUTABLE;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				desc.CPUAccessFlags = 0;
				desc.MiscFlags = 0;

				D3D11_SUBRESOURCE_DATA data{};
				data.pSysMem = src.pixels;
				data.SysMemPitch = (u32)src.rowPitch;

				hr = device->CreateTexture2D(&desc, &data, env_map.ReleaseAndGetAddressOf());
				if (FAILED(hr))
				{
					return hr;
				}
			}

			ComPtr<ID3D11ShaderResourceView> env_map_srv{};
			hr = device->CreateShaderResourceView(env_map.Get(), nullptr, env_map_srv.ReleaseAndGetAddressOf());
			if (FAILED(hr))
			{
				return hr;
			}

			const u32 block_size{ (cubemap_size + 15) >> 4 };
			dispatch(ctx.Get(), env_map_srv.GetAddressOf(), cubemap_uav.GetAddressOf(), constant_buffer.GetAddressOf(),
					 linear_sampler.GetAddressOf(), shader.Get(), { block_size, block_size, 6 });
		}

		reset_d3d11_context(ctx.Get());
		return download_texture_2d(ctx.Get(), cubemap_size, cubemap_size, array_size, 1, format, true,
								   cubemaps.Get(), cubemaps_cpu.Get(), cubemaps_out);
	}

	HRESULT prefilter_diffuse(const ScratchImage& cubemaps, ScratchImage& prefiltered_diffuse)
	{
		const TexMetadata& metadata{ cubemaps.GetMetadata() };
		const u32 array_size{ (u32)metadata.arraySize };
		const u32 cubemap_count{ array_size / 6 };
		assert(metadata.IsCubemap() && cubemap_count && !(array_size % 6));
		assert(metadata.width == metadata.height && !IsCompressed(metadata.format));
		const DXGI_FORMAT format{ metadata.format };
		HRESULT hr{ S_OK };

		ScratchImage resized{};
		hr = limit_prefilter_diffuse_input(cubemaps, resized);
		if (FAILED(hr))
		{
			return hr;
		}

		const ScratchImage& input{ resized.GetImageCount() ? resized : cubemaps };
		const DXGI_FORMAT input_format{ input.GetMetadata().format };
		const u32 src_size{ (u32)input.GetMetadata().width };

		ScratchImage working_scratch{};
		hr = working_scratch.InitializeCube(DXGI_FORMAT_R32G32B32A32_FLOAT, prefiltered_diffuse_cubemap_size, prefiltered_diffuse_cubemap_size, cubemap_count, 1);
		if (FAILED(hr))
		{
			return hr;
		}

		const utl::vector<cubemap_texel> texels{ get_cubemap_texels(src_size) };

		for (u32 i{ 0 }; i < cubemap_count; ++i)
		{
			// サンプリングするのはミップ0だけ。サンプリングしやすいように、32ビット浮動小数点のリニアな色空間に変換する。
			ScratchImage f32_faces[6]{};
			const Image* src_faces[6]{};
			for (u32 face{ 0 }; face < 6; ++face)
			{
				const Image* const image{ input.GetImage(0, i * 6 + face, 0) };
				assert(image);
				if (input_format == DXGI_FORMAT_R32G32B32A32_FLOAT)
				{
					src_faces[face] = image;
					continue;
				}

				hr = Convert(*image, DXGI_FORMAT_R32G32B32A32_FLOAT, TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, f32_faces[face]);
				if (FAILED(hr))
				{
					return hr;
				}

				src_faces[face] = f32_faces[face].GetImages();
			}

			const Image* dst_images{ &working_scratch.GetImages()[i * 6] };

			std::thread threads[]{
				std::thread{ [&] { prefilter_diffuse_face(texels, src_faces, dst_images[0], 0); } },
				std::thread{ [&] { prefilter_diffuse_face(texels, src_faces, dst_images[1], 1); } },
				std::thread{ [&] { prefilter_diffuse_face(texels, src_faces, dst_images[2], 2); } },
				std::thread{ [&] { prefilter_diffuse_face(texels, src_faces, dst_images[3], 3); } },
				std::thread{ [&] { prefilter_diffuse_face(texels, src_faces, dst_images[4], 4); } },
			};

			prefilter_diffuse_face(texels, src_faces, dst_images[5], 5);

			for (u32 face{ 0 }; face < 5; ++face) threads[face].join();
		}

		if (format != DXGI_FORMAT_R32G32B32A32_FLOAT)
		{
			hr = Convert(working_scratch.GetImages(), working_scratch.GetImageCount(), working_scratch.GetMetadata(), format,
						 TEX_FILTER_DEFAULT, TEX_THRESHOLD_DEFAULT, prefiltered_diffuse);
		}
		else
		{
			prefiltered_diffuse = std::move(working_scratch);
		}

		return hr;
	}

	HRESULT prefilter_diffuse(ID3D11Device* device, const ScratchImage& cubemaps, ScratchImage& prefiltered_diffuse)
	{
		const TexMetadata& metadata{ cubemaps.GetMetadata() };
		const u32 array_size{ (u32)metadata.arraySize };
		const u32 cubemap_count{ array_size / 6 };
		assert(device && metadata.IsCubemap() && cubemap_count && !(array_size % 6));
		assert(metadata.width == metadata.height && !IsCompressed(metadata.format));
		const DXGI_FORMAT format{ metadata.format };
		ComPtr<ID3D11DeviceContext> ctx{};
		device->GetImmediateContext(ctx.GetAddressOf());
		assert(ctx.Get());

		HRESULT hr{ S_OK };

		ScratchImage resized{};
		hr = limit_prefilter_diffuse_input(cubemaps, resized);
		if (FAILED(hr))
		{
			return hr;
		}

		const ScratchImage& input{ resized.GetImageCount() ? resized : cubemaps };
		const TexMetadata& input_metadata{ input.GetMetadata() };

		// 出力先のリソースを作成
		ComPtr<ID3D11Texture2D> cubemaps_out{};
		ComPtr<ID3D11Texture2D> cubemaps_cpu{};
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = prefiltered_diffuse_cubemap_size;
			desc.MipLevels = 1;
			desc.ArraySize = array_size;
			desc.Format = format;
			desc.SampleDesc = { 1, 0 };
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			desc.CPUAccessFlags = 0;
			desc.MiscFlags = 0;

			hr = device->CreateTexture2D(&desc, nullptr, cubemaps_out.GetAddressOf());
			if (FAILED(hr))
			{
				return hr;
			}

			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

			hr = device->CreateTexture2D(&desc, nullptr, cubemaps_cpu.GetAddressOf());
			if (FAILED(hr))
			{
				return hr;
			}
		}

		ComPtr<ID3D11ComputeShader> shader{};
		hr = device->CreateComputeShader(shaders::EnvMapProcessing_PrefilterDiffuseEnvMapCS,
										 sizeof(shaders::EnvMapProcessing_PrefilterDiffuseEnvMapCS),
										 nullptr, shader.GetAddressOf());
		if (FAILED(hr))
		{
			return hr;
		}

		ComPtr<ID3D11Buffer> constant_buffer{};
		{
			hr = create_constant_buffer(device, constant_buffer.GetAddressOf());
			if (FAILED(hr))
			{
				return hr;
			}

			ShaderConstants constants{};
			constants.CubeMapInSize = (u32)input_metadata.width;
			constants.CubeMapOutSize = prefiltered_diffuse_cubemap_size;
			hr = set_constants(ctx.Get(), constant_buffer.Get(), constants);
			if (FAILED(hr))
			{
				return hr;
			}
		}

		ComPtr<ID3D11SamplerState> linear_sampler{};
		hr = create_linear_sampler(device, linear_sampler.GetAddressOf());
		if (FAILED(hr))
		{
			return hr;
		}

		reset_d3d11_context(ctx.Get());

		for (u32 i{ 0 }; i < cubemap_count; ++i)
		{
			// 入力のキューブマップをGPUにアップロード。サンプリングするのはミップ0だけなので、ミップ0の6面だけをアップロードする。
			// NOTE: Primalはすべてのキューブマップを1つのテクスチャにまとめ、TEXTURECUBEARRAYのビューを
			//       シェーダのTextureCubeにバインドしていたが、ビューの種類がシェーダの宣言と一致するように
			//       キューブマップごとにTEXTURECUBEのテクスチャを作る。
			ComPtr<ID3D11Texture2D> cubemap_in{};
			{
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = (u32)input_metadata.width;
				desc.Height = (u32)input_metadata.height;
				desc.MipLevels = 1;
				desc.ArraySize = 6;
				desc.Format = input_metadata.format;
				desc.SampleDesc = { 1, 0 };
				desc.Usage = D3D11_USAGE_IMMUTABLE;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				desc.CPUAccessFlags = 0;
				desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

				D3D11_SUBRESOURCE_DATA data[6]{};
				for (u32 face{ 0 }; face < 6; ++face)
				{
					const Image* const image{ input.GetImage(0, i * 6 + face, 0) };
					assert(image);
					data[face].pSysMem = image->pixels;
					data[face].SysMemPitch = (u32)image->rowPitch;
				}

				hr = device->CreateTexture2D(&desc, &data[0], cubemap_in.GetAddressOf());
				if (FAILED(hr))
				{
					return hr;
				}
			}

			ComPtr<ID3D11ShaderResourceView> cubemap_in_srv{};
			hr = device->CreateShaderResourceView(cubemap_in.Get(), nullptr, cubemap_in_srv.GetAddressOf());
			if (FAILED(hr))
			{
				return hr;
			}

			ComPtr<ID3D11UnorderedAccessView> cubemap_out_uav{};
			hr = create_texture_2d_uav(device, format, 6, i * 6, 0, cubemaps_out.Get(), cubemap_out_uav.GetAddressOf());
			if (FAILED(hr))
			{
				return hr;
			}

			const u32 block_size{ (prefiltered_diffuse_cubemap_size + 15) >> 4 };
			dispatch(ctx.Get(), cubemap_in_srv.GetAddressOf(), cubemap_out_uav.GetAddressOf(), constant_buffer.GetAddressOf(),
					 linear_sampler.GetAddressOf(), shader.Get(), { block_size, block_size, 6 });
		}

		reset_d3d11_context(ctx.Get());
		return download_texture_2d(ctx.Get(), prefiltered_diffuse_cubemap_size, prefiltered_diffuse_cubemap_size, array_size, 1, format, true,
								   cubemaps_out.Get(), cubemaps_cpu.Get(), prefiltered_diffuse);
	}

	HRESULT prefilter_specular(const ScratchImage& cubemaps, ScratchImage& prefiltered_specular)
	{
		const TexMetadata& metadata{ cubemaps.GetMetadata() };
		const u32 array_size{ (u32)metadata.arraySize };
		const u32 cubemap_count{ array_size / 6 };
		assert(metadata.IsCubemap() && cubemap_count && !(array_size % 6));
		assert(metadata.width == metadata.height && !IsCompressed(metadata.format));
		HRESULT hr{ S_OK };

		ScratchImage input{};
		hr = prepare_prefilter_specular_input(cubemaps, input);
		if (FAILED(hr))
		{
			return hr;
		}

		ScratchImage working_scratch{};
		hr = working_scratch.InitializeCube(DXGI_FORMAT_R32G32B32A32_FLOAT, prefiltered_specular_cubemap_size, prefiltered_specular_cubemap_size,
										   cubemap_count, roughness_mip_levels);
		if (FAILED(hr))
		{
			return hr;
		}

		copy_roughness_zero_mip(input, working_scratch);

		const specular_input sampler{ input, (u32)input.GetMetadata().mipLevels };

		for (u32 mip{ 1 }; mip < roughness_mip_levels; ++mip)
		{
			const utl::vector<specular_sample> samples{ get_specular_samples(roughness_of_mip(mip), prefilter_specular_sample_count,
																			   prefiltered_specular_cubemap_size) };
			const u32 size{ std::max(prefiltered_specular_cubemap_size >> mip, 1u) };
			const f32 inv_size{ 1.f / (f32)size };

			// すべてのキューブマップの全面の行を、CPUのコアの数のスレッドで分けて処理する。
			parallel_for(array_size * size, [&](u32 row_index)
				{
					const u32 image_index{ row_index / size };
					const u32 y{ row_index % size };
					const u32 face{ image_index % 6 };
					const Image& dst_face{ *working_scratch.GetImage(mip, image_index, 0) };
					f32* const dst_row{ (f32*)(dst_face.pixels + dst_face.rowPitch * y) };
					const f32 v{ ((y + sample_offset) * inv_size) * 2.f - 1.f };

					for (u32 x{ 0 }; x < size; ++x)
					{
						const f32 u{ ((x + sample_offset) * inv_size) * 2.f - 1.f };
						f32* const dst{ &dst_row[x * 4] };
						prefilter_specular_texel(sampler, image_index / 6, samples, get_sample_direction_cubemap(face, u, v), dst);
						dst[3] = 1.f;
					}
				});
		}

		return convert_prefiltered_output(working_scratch, metadata.format, prefiltered_specular);
	}

	HRESULT prefilter_specular(ID3D11Device* device, const ScratchImage& cubemaps, ScratchImage& prefiltered_specular)
	{
		const TexMetadata& metadata{ cubemaps.GetMetadata() };
		const u32 array_size{ (u32)metadata.arraySize };
		const u32 cubemap_count{ array_size / 6 };
		assert(device && metadata.IsCubemap() && cubemap_count && !(array_size % 6));
		assert(metadata.width == metadata.height && !IsCompressed(metadata.format));
		ComPtr<ID3D11DeviceContext> ctx{};
		device->GetImmediateContext(ctx.GetAddressOf());
		assert(ctx.Get());

		HRESULT hr{ S_OK };

		// CPU版と同じ入力を使う。フォーマットはDXGI_FORMAT_R32G32B32A32_FLOATにそろうので、UAVに使えないフォーマット(sRGBなど)で失敗することもない。
		ScratchImage input{};
		hr = prepare_prefilter_specular_input(cubemaps, input);
		if (FAILED(hr))
		{
			return hr;
		}

		const TexMetadata& input_metadata{ input.GetMetadata() };
		const u32 input_mip_levels{ (u32)input_metadata.mipLevels };
		constexpr DXGI_FORMAT format{ DXGI_FORMAT_R32G32B32A32_FLOAT };
		constexpr u32 size{ prefiltered_specular_cubemap_size };

		// 出力先のリソースを作成
		ComPtr<ID3D11Texture2D> cubemaps_out{};
		ComPtr<ID3D11Texture2D> cubemaps_cpu{};
		{
			D3D11_TEXTURE2D_DESC desc{};
			desc.Width = desc.Height = size;
			desc.MipLevels = roughness_mip_levels;
			desc.ArraySize = array_size;
			desc.Format = format;
			desc.SampleDesc = { 1, 0 };
			desc.Usage = D3D11_USAGE_DEFAULT;
			desc.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
			desc.CPUAccessFlags = 0;
			desc.MiscFlags = 0;

			hr = device->CreateTexture2D(&desc, nullptr, cubemaps_out.GetAddressOf());
			if (FAILED(hr))
			{
				return hr;
			}

			desc.BindFlags = 0;
			desc.Usage = D3D11_USAGE_STAGING;
			desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

			hr = device->CreateTexture2D(&desc, nullptr, cubemaps_cpu.GetAddressOf());
			if (FAILED(hr))
			{
				return hr;
			}
		}

		ComPtr<ID3D11ComputeShader> shader{};
		hr = device->CreateComputeShader(shaders::EnvMapProcessing_PrefilterSpecularEnvMapCS,
										 sizeof(shaders::EnvMapProcessing_PrefilterSpecularEnvMapCS),
										 nullptr, shader.GetAddressOf());
		if (FAILED(hr))
		{
			return hr;
		}

		ComPtr<ID3D11Buffer> constant_buffer{};
		hr = create_constant_buffer(device, constant_buffer.GetAddressOf());
		if (FAILED(hr))
		{
			return hr;
		}

		ComPtr<ID3D11SamplerState> linear_sampler{};
		hr = create_linear_sampler(device, linear_sampler.GetAddressOf());
		if (FAILED(hr))
		{
			return hr;
		}

		reset_d3d11_context(ctx.Get());

		for (u32 i{ 0 }; i < cubemap_count; ++i)
		{
			// 入力のキューブマップをすべてのミップと一緒にアップロードする(サンプルの確率密度に応じてミップを選ぶため)。
			// NOTE: Primalはすべてのキューブマップを1つのテクスチャにまとめ、TEXTURECUBEARRAYのビューを
			//       シェーダのTextureCubeにバインドしていたが、拡散光と同じくキューブマップごとにTEXTURECUBEのテクスチャを作る。
			ComPtr<ID3D11Texture2D> cubemap_in{};
			{
				D3D11_TEXTURE2D_DESC desc{};
				desc.Width = (u32)input_metadata.width;
				desc.Height = (u32)input_metadata.height;
				desc.MipLevels = input_mip_levels;
				desc.ArraySize = 6;
				desc.Format = format;
				desc.SampleDesc = { 1, 0 };
				desc.Usage = D3D11_USAGE_IMMUTABLE;
				desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
				desc.CPUAccessFlags = 0;
				desc.MiscFlags = D3D11_RESOURCE_MISC_TEXTURECUBE;

				utl::vector<D3D11_SUBRESOURCE_DATA> data(6ull * input_mip_levels);
				for (u32 face{ 0 }; face < 6; ++face)
				{
					for (u32 mip{ 0 }; mip < input_mip_levels; ++mip)
					{
						const Image* const image{ input.GetImage(mip, i * 6 + face, 0) };
						assert(image);
						D3D11_SUBRESOURCE_DATA& subresource{ data[face * input_mip_levels + mip] };
						subresource.pSysMem = image->pixels;
						subresource.SysMemPitch = (u32)image->rowPitch;
					}
				}

				hr = device->CreateTexture2D(&desc, data.data(), cubemap_in.GetAddressOf());
				if (FAILED(hr))
				{
					return hr;
				}
			}

			ComPtr<ID3D11ShaderResourceView> cubemap_in_srv{};
			hr = device->CreateShaderResourceView(cubemap_in.Get(), nullptr, cubemap_in_srv.GetAddressOf());
			if (FAILED(hr))
			{
				return hr;
			}

			// NOTE: ミップ0(ラフネス0)は入力のミップ0と同じなので、フィルタせずに後でコピーする(Primalと同じ)。
			//       Dispatchはミップごとに分ける。1回あたり最大(128 x 128 x 6画素) x 1024サンプルで、入力のサイズによらない。
			for (u32 mip{ 1 }; mip < roughness_mip_levels; ++mip)
			{
				ComPtr<ID3D11UnorderedAccessView> cubemap_out_uav{};
				hr = create_texture_2d_uav(device, format, 6, i * 6, mip, cubemaps_out.Get(), cubemap_out_uav.GetAddressOf());
				if (FAILED(hr))
				{
					return hr;
				}

				const u32 mip_size{ std::max(size >> mip, 1u) };
				ShaderConstants constants{};
				constants.CubeMapInSize = (u32)input_metadata.width;
				constants.CubeMapOutSize = mip_size;
				constants.SampleCount = prefilter_specular_sample_count;
				constants.Roughness = roughness_of_mip(mip);
				hr = set_constants(ctx.Get(), constant_buffer.Get(), constants);
				if (FAILED(hr))
				{
					return hr;
				}

				const u32 block_size{ (mip_size + 15) >> 4 };
				dispatch(ctx.Get(), cubemap_in_srv.GetAddressOf(), cubemap_out_uav.GetAddressOf(), constant_buffer.GetAddressOf(),
						 linear_sampler.GetAddressOf(), shader.Get(), { block_size, block_size, 6 });
			}
		}

		reset_d3d11_context(ctx.Get());

		ScratchImage working_scratch{};
		hr = download_texture_2d(ctx.Get(), size, size, array_size, roughness_mip_levels, format, true,
								 cubemaps_out.Get(), cubemaps_cpu.Get(), working_scratch);
		if (FAILED(hr))
		{
			return hr;
		}

		copy_roughness_zero_mip(input, working_scratch);

		return convert_prefiltered_output(working_scratch, metadata.format, prefiltered_specular);
	}
}
