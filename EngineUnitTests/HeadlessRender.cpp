// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [HeadlessRender.cpp]
// 作成日 : 2026/10/29
// 作成者 : 田中ミノル
// 概要
//	ウィンドウを作らずに固定のテストシーンを画面外のサーフェスに描画し、PNGに書き出す実行経路(--render)
//	シーン(球と床、平行光源と点光源、カメラ)はすべてコードで組み立てるので、リポジトリの外のデータを使わない。
// 更新履歴
// 2026/10/29 新規作成 (#127)
// 2026/10/29 幅・高さの最小をライトカリングのタイルの大きさにした (#128 のレビュー)
// 2026/10/29 初期化に失敗したときに graphics::shutdown() を重ねて呼ばないようにした (#128 のレビュー)
// 2026/10/29 予期しない例外を最上位で捕捉し、段階に応じた終了コードとJSONを返すようにした (#128 のレビュー)
// 2026/10/30 メッシュとマテリアルを、エディタの形のデータからDLLと同じ経路(ResourceUpload)で作るようにし、
//            テクスチャ・マテリアル・メッシュを作って破棄し、作り直す確認を足した (Primal Day 077, #30)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "HeadlessRender.h"
#include "CommonHeaders.h"
#include "Core/Headless.h"
#include "Graphics/Renderer.h"
#include "Content/ContentToEngine.h"
#include "Components/Entity.h"
#include "Components/Transform.h"
#include "Components/Geometry.h"
#include "../DXForgeEngineDLL/ShaderCompilation.h"
#include "../DXForgeEngineDLL/ResourceUpload.h"
#include "EditorAssetPacking.h"

#include <cmath>
#include <cstdio>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <string>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <wincodec.h>
#include <wrl.h>

#pragma comment(lib, "windowscodecs.lib")

using namespace Microsoft::WRL;

namespace dxforge::headless::render
{
	namespace
	{
		constexpr u32 min_surface_size{ graphics::min_offscreen_surface_size };	// ライトカリングのタイルの大きさ(16)
		constexpr u32 max_surface_size{ 16384 };	// D3D12のテクスチャの最大の幅・高さ

		// 頂点の要素の種類(ContentTools/Geometry.h の elements::elements_type::static_normal と同じ値)
		constexpr u32 elements_type_static_normal{ 0x01 };

		// 引数の解析結果
		struct options
		{
			std::wstring	screenshot{};
			u32				frames{ 0 };
			u32				width{ 0 };
			u32				height{ 0 };
			graphics::adapter_type::type adapter{ graphics::adapter_type::warp };
			bool			has_frames{ false };
			bool			has_width{ false };
			bool			has_height{ false };
			bool			as_json{ false };
		};

		// 実行結果
		struct run_result
		{
			int				exit_code{ exit_code::success };
			std::string		error_code{};		// 失敗したときの識別名(例: "shaders_unavailable")
			std::string		error_message{};	// 失敗したときの説明
			u32				frames_run{ 0 };
			std::string		adapter_name{};
			std::string		pixel_hash{};		// 書き出したRGB(8ビット)のFNV-1a(64ビット)
		};

		// 実行中の段階。予期しない例外を捕捉したとき、どの終了コードにするかを決めるのに使う
		enum class run_stage
		{
			shaders,		// 引数のあと〜シェーダーの用意(DXC、エンジンとマテリアルのシェーダーのコンパイル) → 21
			graphics_init,	// graphics::initialize() → 20
			render,			// テストシーンの作成、描画、読み戻し → 22
			screenshot,		// 8ビットへの変換とPNGの書き出し → 23
		};

		struct run_state
		{
			run_stage	stage{ run_stage::shaders };
			bool		graphics_initialized{ false };	// graphics::shutdown()を呼ぶ必要があるか(1回だけ呼ぶため)
		};

		/// @brief UTF-16の文字列をUTF-8に変換する
		std::string to_utf8(const std::wstring& text)
		{
			if (text.empty()) return {};
			const int length{ WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr) };
			std::string result((size_t)length, '\0');
			WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), result.data(), length, nullptr, nullptr);
			return result;
		}

		/// @brief JSONの文字列として書けるようにエスケープする(UTF-8のまま)
		std::string escape_json(const std::string& text)
		{
			std::string result{};
			result.reserve(text.size());
			for (const char c : text)
			{
				switch (c)
				{
				case '"':	result += "\\\""; break;
				case '\\':	result += "\\\\"; break;
				case '\n':	result += "\\n"; break;
				case '\r':	result += "\\r"; break;
				case '\t':	result += "\\t"; break;
				default:
					if ((unsigned char)c < 0x20)
					{
						char buffer[8];
						std::snprintf(buffer, sizeof(buffer), "\\u%04x", (unsigned)c);
						result += buffer;
					}
					else
					{
						result += c;
					}
					break;
				}
			}
			return result;
		}

		/// @brief 符号なし整数を引数として解析する(全体が数字でなければ失敗)
		bool parse_u32(const wchar_t* text, u32& value)
		{
			if (!text || !*text || *text == L'-' || *text == L'+') return false;
			wchar_t* end{ nullptr };
			errno = 0;
			const unsigned long long parsed{ std::wcstoull(text, &end, 10) };
			if (errno || *end || parsed > 0xffffffffull) return false;
			value = (u32)parsed;
			return true;
		}

		const char* adapter_label(graphics::adapter_type::type adapter)
		{
			return adapter == graphics::adapter_type::warp ? "warp" : "hardware";
		}

		/// @brief 引数を解析する
		/// @return 誤りがあればその説明(なければ空)
		std::string parse_options(int argc, const wchar_t* const* argv, options& opt)
		{
			// NOTE: 引数の誤りも--jsonで返せるように、--jsonだけは先に調べる
			for (int i{ 1 }; i < argc; ++i)
			{
				if (std::wcscmp(argv[i], L"--json") == 0) opt.as_json = true;
			}

			for (int i{ 1 }; i < argc; ++i)
			{
				const std::wstring arg{ argv[i] };
				const bool has_value{ i + 1 < argc };
				if (arg == L"--render")
				{
				}
				else if (arg == L"--json")
				{
					opt.as_json = true;
				}
				else if (arg == L"--frames" || arg == L"--width" || arg == L"--height")
				{
					if (!has_value) return to_utf8(arg) + " requires a value";
					u32 value{ 0 };
					if (!parse_u32(argv[++i], value)) return to_utf8(arg) + " must be a non-negative integer";
					if (arg == L"--frames")
					{
						if (value == 0) return "--frames must be at least 1";
						opt.frames = value;
						opt.has_frames = true;
					}
					else
					{
						if (value < min_surface_size || value > max_surface_size)
						{
							return to_utf8(arg) + " must be between " + std::to_string(min_surface_size) + " and " + std::to_string(max_surface_size);
						}
						(arg == L"--width" ? opt.width : opt.height) = value;
						(arg == L"--width" ? opt.has_width : opt.has_height) = true;
					}
				}
				else if (arg == L"--screenshot")
				{
					if (!has_value) return "--screenshot requires a value";
					opt.screenshot = argv[++i];
					if (opt.screenshot.empty()) return "--screenshot must not be empty";
				}
				else if (arg == L"--adapter")
				{
					if (!has_value) return "--adapter requires a value";
					const std::wstring value{ argv[++i] };
					if (value == L"warp") opt.adapter = graphics::adapter_type::warp;
					else if (value == L"hardware") opt.adapter = graphics::adapter_type::default_adapter;
					else return "--adapter must be warp or hardware";
				}
				else
				{
					return "unknown argument: " + to_utf8(arg);
				}
			}

			if (!opt.has_frames) return "--frames is required";
			if (!opt.has_width) return "--width is required";
			if (!opt.has_height) return "--height is required";
			if (opt.screenshot.empty()) return "--screenshot is required";
			return {};
		}

		/// @brief この実行ファイルのフォルダ
		std::filesystem::path executable_folder()
		{
			std::wstring path(MAX_PATH, L'\0');
			for (;;)
			{
				const DWORD length{ GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size()) };
				if (length == 0) return {};
				if (length < path.size())
				{
					path.resize(length);
					break;
				}
				path.resize(path.size() * 2);
			}
			return std::filesystem::path{ path }.parent_path();
		}

		// ---- テストシーンのメッシュ ----

		struct mesh_data
		{
			std::vector<math::v3>	positions{};
			std::vector<math::v3>	normals{};
			std::vector<u16>		indices{};
		};

		/// @brief [-1, 1]の値を16ビットに詰める(TestShader.hlslの InvIntervals = 2 / 65535 で戻す)
		u16 pack_unit_float(f32 value)
		{
			const f32 clamped{ value < -1.f ? -1.f : (value > 1.f ? 1.f : value) };
			return (u16)std::lround((clamped + 1.f) * 0.5f * 65535.f);
		}

		/// @brief ContentToEngineのcreate_resource(asset_type::mesh)が読む形式(LOD 1つ、サブメッシュ1つ)にする
		/// @details lod_count, threshold, submesh_count, submeshesのサイズ, サブメッシュ
		///			 サブメッシュ: element_size, vertex_count, index_count, elements_type, primitive_topology,
		///			 位置(v3), 要素(static_normal), インデックス(u16)。D3D12Content.cppのsubmesh::add()と同じ並び。
		std::vector<u8> pack_mesh(const mesh_data& mesh)
		{
			// ContentTools/Geometry.h の elements::static_normal と同じ並び(8バイト)
			struct static_normal
			{
				u8 color[3];
				u8 t_sign;		// bit 2: normal.zの符号(0なら-1、1なら+1)
				u16 normal[2];
			};
			static_assert(sizeof(static_normal) == 8);

			const u32 vertex_count{ (u32)mesh.positions.size() };
			const u32 index_count{ (u32)mesh.indices.size() };
			assert(vertex_count < (1 << 16));
			const u32 position_size{ (u32)sizeof(math::v3) * vertex_count };
			const u32 element_size{ (u32)sizeof(static_normal) * vertex_count };
			const u32 index_size{ (u32)sizeof(u16) * index_count };
			// NOTE: 位置(12バイト)と要素(8バイト)のサイズは4の倍数なので、D3D12_STANDARD_MAXIMUM_ELEMENT_ALIGNMENT_BYTE_MULTIPLE(4)の詰め物は要らない
			const u32 submesh_size{ 5 * (u32)sizeof(u32) + position_size + element_size + index_size };

			std::vector<u8> blob{};
			auto write = [&blob](const void* data, size_t size)
			{
				const u8* const bytes{ (const u8*)data };
				blob.insert(blob.end(), bytes, bytes + size);
			};
			auto write_u32 = [&write](u32 value) { write(&value, sizeof(value)); };

			const f32 threshold{ 0.f };
			write_u32(1);					// lod_count
			write(&threshold, sizeof(threshold));
			write_u32(1);					// submesh_count
			write_u32(submesh_size);
			write_u32((u32)sizeof(static_normal));
			write_u32(vertex_count);
			write_u32(index_count);
			write_u32(elements_type_static_normal);
			write_u32(graphics::primitive_topology::triangle_list);
			write(mesh.positions.data(), position_size);
			for (const math::v3& n : mesh.normals)
			{
				static_normal element{ { 255, 255, 255 }, (u8)(n.z > 0.f ? 0x04 : 0x00), { pack_unit_float(n.x), pack_unit_float(n.y) } };
				write(&element, sizeof(element));
			}
			write(mesh.indices.data(), index_size);
			return blob;
		}

		/// @brief 原点を中心とする球(緯度・経度で分割)
		mesh_data make_sphere(f32 radius, u32 slices, u32 stacks)
		{
			mesh_data mesh{};
			for (u32 j{ 0 }; j <= stacks; ++j)
			{
				const f32 theta{ math::pi * (f32)j / (f32)stacks };	// 北極(+Y)からの角度
				for (u32 i{ 0 }; i <= slices; ++i)
				{
					const f32 phi{ math::two_pi * (f32)i / (f32)slices };
					const math::v3 n{ std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi) };
					mesh.normals.push_back(n);
					mesh.positions.push_back({ n.x * radius, n.y * radius, n.z * radius });
				}
			}
			for (u32 j{ 0 }; j < stacks; ++j)
			{
				for (u32 i{ 0 }; i < slices; ++i)
				{
					const u16 a{ (u16)(j * (slices + 1) + i) };
					const u16 b{ (u16)(a + slices + 1) };
					mesh.indices.insert(mesh.indices.end(), { a, (u16)(a + 1), b, (u16)(a + 1), (u16)(b + 1), b });
				}
			}
			return mesh;
		}

		/// @brief 原点を中心とする、上(+Y)を向いた正方形の板
		mesh_data make_floor(f32 half_size)
		{
			mesh_data mesh{};
			mesh.positions = { { -half_size, 0.f, -half_size }, { -half_size, 0.f, half_size }, { half_size, 0.f, half_size }, { half_size, 0.f, -half_size } };
			mesh.normals = { { 0.f, 1.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, 1.f, 0.f }, { 0.f, 1.f, 0.f } };
			mesh.indices = { 0, 1, 2, 0, 2, 3 };
			return mesh;
		}

		// ---- テストシーン ----

		class test_scene
		{
		public:
			DISABLE_COPY_AND_MOVE(test_scene);
			test_scene() = default;
			~test_scene() { destroy(); }

			/// @brief マテリアルのシェーダー(EngineTest/TestShader.hlsl。EngineTestと同じ)をコンパイルする
			/// @return 失敗したときはその説明(成功なら空)
			std::string create_shaders()
			{
				constexpr const char* shader_path{ "..\\..\\EngineTest\\" };
				shader_file_info info{};
				info.file_name = "TestShader.hlsl";
				info.function = "TestShaderVS";
				info.type = graphics::shader_type::vertex;
				utl::vector<std::wstring> extra_args{};
				extra_args.emplace_back(L"-D");
				extra_args.emplace_back(L"ELEMENTS_TYPE=1");
				const std::unique_ptr<u8[]> vertex_shader{ compile_shader(info, shader_path, extra_args) };

				info.function = "TestShaderPS";
				info.type = graphics::shader_type::pixel;
				extra_args.clear();
				const std::unique_ptr<u8[]> pixel_shader{ compile_shader(info, shader_path, extra_args) };
				if (!vertex_shader || !pixel_shader) return "failed to compile EngineTest/TestShader.hlsl";

				const u8* vertex_shaders[]{ vertex_shader.get() };
				const u32 vertex_keys[]{ elements_type_static_normal };
				_vs_id = content::add_shader_group(vertex_shaders, 1, vertex_keys);
				const u8* pixel_shaders[]{ pixel_shader.get() };
				_ps_id = content::add_shader_group(pixel_shaders, 1, &u32_invalid_id);
				return {};
			}

			/// @brief メッシュ・マテリアル・ライト・カメラ・エンティティを作る(create_shaders()のあと)
			/// @return 失敗したときはその説明(成功なら空)
			std::string create(f32 aspect_ratio)
			{
				assert(id::is_valid(_vs_id) && id::is_valid(_ps_id));
				const std::vector<u8> sphere_blob{ pack_mesh(make_sphere(1.f, 48, 24)) };
				const std::vector<u8> floor_blob{ pack_mesh(make_floor(4.f)) };
				const std::string upload_error{ check_resource_round_trip(floor_blob) };
				if (!upload_error.empty()) return upload_error;

				// NOTE: エディタから作るときと同じ経路(DLLの CreateResource = editor::create_resource)で作る
				_sphere_model_id = editor::create_resource(sphere_blob.data(), content::asset_type::mesh);
				_floor_model_id = editor::create_resource(floor_blob.data(), content::asset_type::mesh);
				if (!id::is_valid(_sphere_model_id) || !id::is_valid(_floor_model_id)) return "failed to create the test meshes";

				_sphere_mtl_id = create_material({ 0.8f, 0.25f, 0.2f, 1.f }, 0.f, 0.4f);
				_floor_mtl_id = create_material({ 0.6f, 0.6f, 0.6f, 1.f }, 0.f, 0.9f);

				// NOTE: ライトにはエンティティが要る(light::create()のassert)。平行光源はエンティティのfront()の向きに照らす。
				graphics::create_light_set(light_set_key);
				_has_light_set = true;
				_directional_light = create_light(graphics::light::directional, { 0.f, 5.f, 0.f }, { 0.9f, -0.6f, 0.f }, { 1.f, 0.95f, 0.85f }, 1.f);
				_point_light = create_light(graphics::light::point, { -1.6f, 0.4f, -1.4f }, {}, { 0.2f, 0.5f, 1.f }, 4.f);
				if (!_directional_light.is_valid() || !_point_light.is_valid()) return "failed to create the test lights";

				_sphere_entity = create_entity({ 0.f, 0.f, 0.f }, {}, _sphere_model_id, _sphere_mtl_id);
				_floor_entity = create_entity({ 0.f, -1.f, 0.f }, {}, _floor_model_id, _floor_mtl_id);
				if (!_sphere_entity.is_valid() || !_floor_entity.is_valid()) return "failed to create the test entities";

				// カメラは手前(-Z)の少し上から原点を見下ろす(エンティティのfront()はローカルの+Z)
				_camera_entity = create_entity({ 0.f, 1.5f, -5.f }, { 0.3f, 0.f, 0.f }, id::invalid_id, id::invalid_id);
				if (!_camera_entity.is_valid()) return "failed to create the camera entity";
				_camera = graphics::create_camera(graphics::perspective_camera_init_info{ _camera_entity.get_id() });
				if (!_camera.is_valid()) return "failed to create the camera";
				_camera.aspect_ratio(aspect_ratio);

				_render_item_ids.resize(2);
				geometry::get_render_item_ids(_render_item_ids.data(), (u32)_render_item_ids.size());
				_thresholds.resize(_render_item_ids.size(), 0.f);
				return {};
			}

			graphics::frame_info frame_info()
			{
				graphics::frame_info info{};
				info.render_item_ids = _render_item_ids.data();
				info.render_item_count = (u32)_render_item_ids.size();
				info.thresholds = _thresholds.data();
				info.light_set_key = light_set_key;
				info.camera_id = _camera.get_id();
				return info;
			}

			void destroy()
			{
				for (game_entity::entity* entity : { &_sphere_entity, &_floor_entity, &_camera_entity })
				{
					if (entity->is_valid()) game_entity::remove(entity->get_id());
					*entity = {};
				}
				if (_camera.is_valid()) graphics::remove_camera(_camera.get_id());
				_camera = {};
				for (graphics::light* light : { &_directional_light, &_point_light })
				{
					if (light->is_valid())
					{
						const game_entity::entity_id entity_id{ light->entity_id() };
						graphics::remove_light(light->get_id(), light_set_key);
						game_entity::remove(entity_id);
					}
					*light = {};
				}
				if (_has_light_set) graphics::remove_light_set(light_set_key);
				_has_light_set = false;
				for (id::id_type* id : { &_sphere_model_id, &_floor_model_id })
				{
					editor::destroy_resource(*id, content::asset_type::mesh);
					*id = id::invalid_id;
				}
				for (id::id_type* id : { &_sphere_mtl_id, &_floor_mtl_id })
				{
					editor::destroy_resource(*id, content::asset_type::material);
					*id = id::invalid_id;
				}
				for (id::id_type* id : { &_vs_id, &_ps_id })
				{
					if (id::is_valid(*id)) content::remove_shader_group(*id);
					*id = id::invalid_id;
				}
			}

		private:
			constexpr static u64 light_set_key{ 0 };

			/// @brief エディタの AppliedMaterial.PackForEngine() の形のデータから、DLLと同じ経路でマテリアルを作る
			id::id_type create_material(math::v4 base_color, f32 metallic, f32 roughness, const id::id_type* texture_ids = nullptr, u32 texture_count = 0)
			{
				graphics::material_surface surface{};
				surface.base_color = base_color;
				surface.metallic = metallic;
				surface.roughness = roughness;
				id::id_type shader_ids[graphics::shader_type::count];
				for (id::id_type& id : shader_ids) id = id::invalid_id;
				shader_ids[graphics::shader_type::vertex] = _vs_id;
				shader_ids[graphics::shader_type::pixel] = _ps_id;
				const std::vector<u8> data{ test::pack_material(surface, graphics::material_type::opaque, texture_ids, texture_count, shader_ids) };
				return editor::create_resource(data.data(), content::asset_type::material);
			}

			/// @brief エディタの形のデータから、テクスチャ・それを使うマテリアル・メッシュをDLLと同じ経路で作って破棄する。
			///		   これを2回行い、破棄したIDが再利用されても作れることを確かめる(Primal Day 077, #30)
			/// @return 失敗したときはその説明(成功なら空)
			std::string check_resource_round_trip(const std::vector<u8>& mesh_blob)
			{
				constexpr u32 size{ 4 };
				constexpr u32 format_r8g8b8a8_unorm{ 28 };	// DXGI_FORMAT_R8G8B8A8_UNORM
				std::vector<u8> pixels(size * size * 4);
				for (u32 i{ 0 }; i < (u32)pixels.size(); ++i) pixels[i] = (u8)(i * 13);
				const std::vector<u8> texture_blob{ test::pack_texture_2d(size, size, format_r8g8b8a8_unorm, size * 4, pixels) };

				id::id_type previous_ids[3]{ id::invalid_id, id::invalid_id, id::invalid_id };
				u32 reused{ 0 };
				for (u32 round{ 0 }; round < 2; ++round)
				{
					const id::id_type texture_id{ editor::create_resource(texture_blob.data(), content::asset_type::texture) };
					const id::id_type material_id{ id::is_valid(texture_id) ? create_material({ 1.f, 1.f, 1.f, 1.f }, 0.5f, 0.5f, &texture_id, 1) : id::invalid_id };
					const id::id_type mesh_id{ editor::create_resource(mesh_blob.data(), content::asset_type::mesh) };
					const bool created{ id::is_valid(texture_id) && id::is_valid(material_id) && id::is_valid(mesh_id) };

					editor::destroy_resource(material_id, content::asset_type::material);
					editor::destroy_resource(texture_id, content::asset_type::texture);
					editor::destroy_resource(mesh_id, content::asset_type::mesh);
					if (!created) return "failed to create a texture, a material or a mesh from the editor's data (round " + std::to_string(round + 1) + ")";

					const id::id_type ids[3]{ texture_id, material_id, mesh_id };
					for (u32 i{ 0 }; i < 3; ++i)
					{
						if (ids[i] == previous_ids[i]) ++reused;
						previous_ids[i] = ids[i];
					}
				}

				std::fprintf(stderr, "[render] created and destroyed a texture, a material and a mesh from the editor's data twice (%u of 3 ids reused)\n", reused);
				return {};
			}

			/// @brief エンティティを作る(model_idが有効ならジオメトリも付ける)
			/// @param rotation オイラー角(ピッチ、ヨー、ロール。ラジアン)
			game_entity::entity create_entity(math::v3 position, math::v3 rotation, id::id_type model_id, id::id_type material_id)
			{
				transform::init_info transform_info{};
				math::v4a quaternion{};
				DirectX::XMStoreFloat4A(&quaternion, DirectX::XMQuaternionRotationRollPitchYaw(rotation.x, rotation.y, rotation.z));
				memcpy(&transform_info.rotation[0], &quaternion.x, sizeof(transform_info.rotation));
				memcpy(&transform_info.position[0], &position.x, sizeof(transform_info.position));

				id::id_type material_ids[]{ material_id };
				geometry::init_info geometry_info{};
				geometry_info.geometry_content_id = model_id;
				geometry_info.material_count = 1;
				geometry_info.material_ids = &material_ids[0];

				game_entity::entity_info entity_info{};
				entity_info.transform = &transform_info;
				entity_info.geometry = id::is_valid(model_id) ? &geometry_info : nullptr;
				return game_entity::create(entity_info);
			}

			graphics::light create_light(graphics::light::type type, math::v3 position, math::v3 rotation, math::v3 color, f32 range)
			{
				const game_entity::entity entity{ create_entity(position, rotation, id::invalid_id, id::invalid_id) };
				if (!entity.is_valid()) return {};
				graphics::light_init_info info{};
				info.entity_id = entity.get_id();
				info.type = type;
				info.light_set_key = light_set_key;
				info.intensity = 1.f;
				info.color = color;
				if (type == graphics::light::point)
				{
					info.point_params.range = range;
					info.point_params.attenuation = { 1.f, 1.f, 1.f };
				}
				return graphics::create_light(info);
			}

			id::id_type _vs_id{ id::invalid_id };
			id::id_type _ps_id{ id::invalid_id };
			id::id_type _sphere_model_id{ id::invalid_id };
			id::id_type _floor_model_id{ id::invalid_id };
			id::id_type _sphere_mtl_id{ id::invalid_id };
			id::id_type _floor_mtl_id{ id::invalid_id };
			bool _has_light_set{ false };
			graphics::light _directional_light{};
			graphics::light _point_light{};
			game_entity::entity _sphere_entity{};
			game_entity::entity _floor_entity{};
			game_entity::entity _camera_entity{};
			graphics::camera _camera{};
			utl::vector<id::id_type> _render_item_ids{};
			utl::vector<f32> _thresholds{};
		};

		// ---- PNGの書き出し ----

		/// @brief リニアの値を8ビットのsRGBにする(トーンマッピングはしない)
		/// @details 描画先(R16G16B16A16_FLOAT)はスワップチェインと同じ形式で、ウィンドウではDWMがリニア(scRGB)として扱い、
		///			 [0, 1]で切ってsRGBで表示する。それと同じ見た目になるように、[0, 1]に収めてからsRGBのOETFをかける。
		u8 linear_to_srgb8(f32 value)
		{
			// NOTE: NaNはここで0になる(比較がすべて偽になるため)
			const f32 c{ value > 0.f ? (value < 1.f ? value : 1.f) : 0.f };
			const f32 srgb{ c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.f / 2.4f) - 0.055f };
			return (u8)std::lround(srgb * 255.f);
		}

		/// @brief 24ビットのBGRの画素をPNGに書く(WIC)。失敗したときは書きかけのファイルを消す。
		/// @return 失敗したときはその説明(成功なら空)
		std::string write_png(const std::wstring& path, u32 width, u32 height, const std::vector<u8>& bgr)
		{
			ComPtr<IWICImagingFactory> factory{};
			if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)))) return "failed to create the WIC imaging factory";

			std::string error{};
			{
				ComPtr<IWICStream> stream{};
				ComPtr<IWICBitmapEncoder> encoder{};
				ComPtr<IWICBitmapFrameEncode> frame{};
				WICPixelFormatGUID format{ GUID_WICPixelFormat24bppBGR };
				if (FAILED(factory->CreateStream(&stream)) || FAILED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)))
				{
					return "failed to open the screenshot file for writing";
				}
				if (FAILED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) ||
					FAILED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) ||
					FAILED(encoder->CreateNewFrame(&frame, nullptr)) ||
					FAILED(frame->Initialize(nullptr)) ||
					FAILED(frame->SetSize(width, height)) ||
					FAILED(frame->SetPixelFormat(&format)))
				{
					error = "failed to initialize the PNG encoder";
				}
				else if (!IsEqualGUID(format, GUID_WICPixelFormat24bppBGR))
				{
					error = "the PNG encoder does not accept 24bpp BGR pixels";
				}
				else if (FAILED(frame->WritePixels(height, width * 3, (UINT)bgr.size(), (BYTE*)bgr.data())) ||
					FAILED(frame->Commit()) || FAILED(encoder->Commit()))
				{
					error = "failed to write the PNG file";
				}
			}
			// NOTE: ストリームを閉じてから消す
			if (!error.empty()) DeleteFileW(path.c_str());
			return error;
		}

		// ---- 実行 ----

		void fail(run_result& result, int code, const char* error_code, const std::string& message)
		{
			result.exit_code = code;
			result.error_code = error_code;
			result.error_message = message;
			std::fprintf(stderr, "[render] %s\n", message.c_str());
		}

		/// @brief デバッグレイヤーのメッセージを標準エラーに出す。ERROR・CORRUPTIONがあれば失敗にする
		bool check_debug_messages(run_result& result, const char* when)
		{
			const u32 error_count{ graphics::flush_debug_messages() };
			if (!error_count) return true;
			fail(result, exit_code::render_failed, "render_failed",
				std::to_string(error_count) + " D3D12 debug layer error(s) " + when + " (see stderr)");
			return false;
		}

		/// @brief 画面外のサーフェスを、例外で抜けたときも含めて必ず取り除く
		class offscreen_surface
		{
		public:
			DISABLE_COPY_AND_MOVE(offscreen_surface);
			offscreen_surface(u32 width, u32 height) : _surface{ graphics::create_offscreen_surface(width, height) } {}
			~offscreen_surface() { remove(); }

			void remove()
			{
				if (_surface.is_valid()) graphics::remove_surface(_surface.get_id());
				_surface = {};
			}

			[[nodiscard]] const graphics::surface& get() const { return _surface; }

		private:
			graphics::surface _surface{};
		};

		/// @brief 描画して読み戻し、PNGに書き出す(graphics::initialize()のあと)
		void render_and_capture(const options& opt, run_result& result, run_state& state)
		{
			// NOTE: 例外で抜けたときは、sceneとsurfaceのデストラクタが、graphics::shutdown()より先に後片付けをする
			test_scene scene{};
			state.stage = run_stage::shaders;
			const std::string shader_error{ scene.create_shaders() };
			if (!shader_error.empty())
			{
				fail(result, exit_code::shaders_unavailable, "shaders_unavailable", shader_error);
				return;
			}

			state.stage = run_stage::render;
			const std::string scene_error{ scene.create((f32)opt.width / (f32)opt.height) };
			if (!scene_error.empty())
			{
				fail(result, exit_code::render_failed, "render_failed", scene_error);
				return;
			}

			offscreen_surface surface{ opt.width, opt.height };
			if (!surface.get().is_valid())
			{
				fail(result, exit_code::render_failed, "render_failed", "failed to create the offscreen surface");
				return;
			}

			for (u32 i{ 0 }; i < opt.frames; ++i)
			{
				surface.get().render(scene.frame_info());
				++result.frames_run;
			}

			utl::vector<math::v4> pixels{};
			const bool read{ surface.get().read_back(pixels) };
			surface.remove();
			if (!check_debug_messages(result, "while rendering")) return;
			if (!read || pixels.size() != (size_t)opt.width * opt.height)
			{
				fail(result, exit_code::render_failed, "render_failed", "failed to read back the offscreen surface");
				return;
			}
			std::fprintf(stderr, "[render] rendered %u frames (%ux%u)\n", result.frames_run, opt.width, opt.height);

			// 8ビットのsRGBに変換する。ハッシュはRGBの順(左上から行ごと)で取る。
			state.stage = run_stage::screenshot;
			std::vector<u8> bgr((size_t)opt.width * opt.height * 3);
			u64 hash{ 0xcbf29ce484222325ull };
			for (size_t i{ 0 }; i < pixels.size(); ++i)
			{
				const u8 rgb[3]{ linear_to_srgb8(pixels[i].x), linear_to_srgb8(pixels[i].y), linear_to_srgb8(pixels[i].z) };
				for (const u8 c : rgb)
				{
					hash ^= c;
					hash *= 0x100000001b3ull;
				}
				bgr[i * 3 + 0] = rgb[2];
				bgr[i * 3 + 1] = rgb[1];
				bgr[i * 3 + 2] = rgb[0];
			}
			char hash_text[17]{};
			std::snprintf(hash_text, sizeof(hash_text), "%016llx", (unsigned long long)hash);
			result.pixel_hash = hash_text;

			const std::string png_error{ write_png(opt.screenshot, opt.width, opt.height, bgr) };
			if (!png_error.empty())
			{
				fail(result, exit_code::screenshot_write_failed, "screenshot_write_failed", png_error + ": " + to_utf8(opt.screenshot));
				return;
			}
			std::fprintf(stderr, "[render] wrote %s\n", to_utf8(opt.screenshot).c_str());
		}

		void run_render(options& opt, run_result& result, run_state& state)
		{
			state.stage = run_stage::shaders;
			// NOTE: エンジンのシェーダー(shaders.bin、ソース)とTestShader.hlslのパスは、EngineTestと同じく作業フォルダからの相対パスなので、
			//		 実行ファイルのフォルダ(x64/<構成>/)に移る。その前に、出力先を今の作業フォルダからの絶対パスにしておく。
			std::error_code error{};
			const std::filesystem::path screenshot{ std::filesystem::absolute(opt.screenshot, error) };
			if (error)
			{
				fail(result, exit_code::screenshot_write_failed, "screenshot_write_failed", "invalid screenshot path: " + to_utf8(opt.screenshot));
				return;
			}
			opt.screenshot = screenshot.wstring();

			const std::filesystem::path exe_folder{ executable_folder() };
			if (exe_folder.empty() || !SetCurrentDirectoryW(exe_folder.c_str()))
			{
				fail(result, exit_code::shaders_unavailable, "shaders_unavailable", "failed to change the working directory to the executable folder");
				return;
			}

			// DXCは実行時にこの実行ファイルと同じフォルダから読み込む(ShaderCompilation.cpp)。リンクはしていない。
			if (!std::filesystem::is_regular_file(exe_folder / L"dxcompiler.dll", error))
			{
				fail(result, exit_code::shaders_unavailable, "shaders_unavailable",
					"dxcompiler.dll was not found next to EngineUnitTests.exe (build EngineTest, or run Restore.ps1 and rebuild EngineUnitTests)");
				return;
			}
			if (!std::filesystem::is_directory(L"..\\..\\DXForgeEngine\\Graphics\\Direct3D12\\Shaders", error) ||
				!std::filesystem::is_regular_file(L"..\\..\\EngineTest\\TestShader.hlsl", error))
			{
				fail(result, exit_code::shaders_unavailable, "shaders_unavailable",
					"shader sources were not found (EngineUnitTests.exe must stay in x64\\<configuration>\\ of the repository)");
				return;
			}
			if (!compile_shaders())
			{
				fail(result, exit_code::shaders_unavailable, "shaders_unavailable", "failed to compile the engine shaders");
				return;
			}

			state.stage = run_stage::graphics_init;
			graphics::graphics_init_info init_info{};
			init_info.adapter = opt.adapter;
			init_info.unattended = true;
			// NOTE: 初期化に失敗したときは、D3D12の側(failed_init())でデバッグレイヤーのメッセージを出してから後片付けまで
			//		 済ませている。ここでもう一度 graphics::shutdown() を呼ぶと、解放済みのフェンスを待ってクラッシュする(#128のレビュー)。
			if (!graphics::initialize(graphics::graphics_platform::direct3d12, init_info))
			{
				fail(result, exit_code::graphics_init_failed, "graphics_init_failed",
					std::string{ "failed to initialize Direct3D 12 with the " } + adapter_label(opt.adapter) + " adapter");
				return;
			}
			state.graphics_initialized = true;
			result.adapter_name = graphics::adapter_name();
			std::fprintf(stderr, "[render] adapter: %s (%s)\n", result.adapter_name.c_str(), adapter_label(opt.adapter));

			render_and_capture(opt, result, state);

			// 後片付けで出たメッセージも出す(失敗の扱いは描画のときのものを優先する)
			state.stage = run_stage::render;
			if (result.exit_code == exit_code::success) check_debug_messages(result, "while cleaning up");
			else graphics::flush_debug_messages();
			state.graphics_initialized = false;
			graphics::shutdown();
		}

		/// @brief 例外のメッセージ(what())を、JSONに入れられるASCIIにする(ASCII以外のバイトは\xNN)
		/// @details std::filesystem_errorのwhat()は、パスをシステムのコードページで含むことがあり、UTF-8とは限らない
		std::string ascii_message(const char* text)
		{
			std::string result{};
			for (const char* c{ text ? text : "" }; *c; ++c)
			{
				const unsigned char byte{ (unsigned char)*c };
				if (byte < 0x80)
				{
					result += *c;
				}
				else
				{
					char buffer[8];
					std::snprintf(buffer, sizeof(buffer), "\\x%02x", (unsigned)byte);
					result += buffer;
				}
			}
			return result;
		}

		/// @brief 予期しない例外を、その段階の終了コードに変換する。初期化済みならgraphics::shutdown()を1回だけ呼ぶ
		void handle_exception(run_result& result, run_state& state, const char* what)
		{
			struct stage_info { int code; const char* error_code; const char* label; };
			stage_info info{};
			switch (state.stage)
			{
			case run_stage::shaders: info = { exit_code::shaders_unavailable, "shaders_unavailable", "preparing the shaders" }; break;
			case run_stage::graphics_init: info = { exit_code::graphics_init_failed, "graphics_init_failed", "initializing graphics" }; break;
			case run_stage::screenshot: info = { exit_code::screenshot_write_failed, "screenshot_write_failed", "writing the screenshot" }; break;
			case run_stage::render:
			default: info = { exit_code::render_failed, "render_failed", "rendering" }; break;
			}
			fail(result, info.code, info.error_code, std::string{ "unexpected exception while " } + info.label + ": " + ascii_message(what));

			if (state.graphics_initialized)
			{
				state.graphics_initialized = false;
				try
				{
					graphics::flush_debug_messages();
					graphics::shutdown();
				}
				catch (...)
				{
					std::fprintf(stderr, "[render] another exception while shutting down graphics\n");
				}
			}
		}

		void print_json_result(const options& opt, const run_result& result)
		{
			std::printf("{\"mode\":\"render\",\"screenshot\":\"%s\",", escape_json(to_utf8(opt.screenshot)).c_str());
			std::printf("\"frames_requested\":%u,\"frames_run\":%u,\"width\":%u,\"height\":%u,", opt.frames, result.frames_run, opt.width, opt.height);
			std::printf("\"adapter\":\"%s\",\"adapter_name\":\"%s\",", adapter_label(opt.adapter), escape_json(result.adapter_name).c_str());
			std::printf("\"success\":%s,\"exit_code\":%d,\"error\":", result.exit_code == exit_code::success ? "true" : "false", result.exit_code);
			if (result.exit_code == exit_code::success)
			{
				std::printf("null");
			}
			else
			{
				std::printf("{\"code\":\"%s\",\"message\":\"%s\"}",
					escape_json(result.error_code).c_str(), escape_json(result.error_message).c_str());
			}
			if (result.pixel_hash.empty()) std::printf(",\"pixel_hash\":null}\n");
			else std::printf(",\"pixel_hash\":\"%s\"}\n", result.pixel_hash.c_str());
		}

		void print_human_result(const options& opt, const run_result& result)
		{
			if (result.exit_code != exit_code::success)
			{
				std::printf("render: FAILED (exit code %d) %s: %s\n", result.exit_code, result.error_code.c_str(), result.error_message.c_str());
				return;
			}
			std::printf("render: OK, %u frames, %ux%u, %s (%s), pixel hash %s\n  %s\n", result.frames_run, opt.width, opt.height,
				adapter_label(opt.adapter), result.adapter_name.c_str(), result.pixel_hash.c_str(), to_utf8(opt.screenshot).c_str());
		}
	}	// 匿名名前空間

	bool is_requested(int argc, const wchar_t* const* argv)
	{
		for (int i{ 1 }; i < argc; ++i)
		{
			if (std::wcscmp(argv[i], L"--render") == 0) return true;
		}
		return false;
	}

	void print_usage()
	{
		std::printf(
			"Headless render (no window; draws a fixed test scene into an offscreen texture and writes a PNG):\n"
			"  --render             select this mode\n"
			"  --frames <N>         number of frames to render before the screenshot (required, N >= 1)\n"
			"  --width <w>          width in pixels (required, 16..16384; 16 is the light culling tile size)\n"
			"  --height <h>         height in pixels (required, 16..16384)\n"
			"  --screenshot <png>   PNG file to write (required; 8-bit sRGB, no tone mapping)\n"
			"  --adapter <a>        warp (default; software rasterizer, deterministic) or hardware (default GPU selection)\n"
			"  --json               print a machine-readable result to stdout (logs go to stderr)\n"
			"  Needs dxcompiler.dll/dxil.dll next to the exe and the shader sources of the repository\n"
			"  (EngineTest/TestShader.hlsl, DXForgeEngine/Graphics/Direct3D12/Shaders) at ..\\..\\ from the exe.\n"
			"Exit codes:\n"
			"  0  success\n"
			"  2  invalid arguments\n"
			"  3  abort() (assert failure etc.)\n"
			"  20 graphics initialization failed (adapter or WARP unavailable)\n"
			"  21 shaders unavailable (dxcompiler.dll or shader sources not found, or compilation failed)\n"
			"  22 rendering or read back failed (including D3D12 debug layer errors in Debug builds)\n"
			"  23 the PNG could not be written\n");
	}

	int run(int argc, const wchar_t* const* argv)
	{
		options opt{};
		const std::string usage_error{ parse_options(argc, argv, opt) };
		run_result result{};
		if (!usage_error.empty())
		{
			fail(result, exit_code::usage_error, "usage_error", usage_error);
		}
		else
		{
			// WICはCOMを使う。既に別のモードで初期化されていればそのまま使う。
			const HRESULT com_result{ CoInitializeEx(nullptr, COINIT_MULTITHREADED) };
			// NOTE: 失敗の経路でも、決めた終了コードとJSONを返すため、予期しない例外(std::filesystemなど)をここで捕捉する(#128のレビュー)
			run_state state{};
			try
			{
				run_render(opt, result, state);
			}
			catch (const std::exception& e)
			{
				handle_exception(result, state, e.what());
			}
			catch (...)
			{
				handle_exception(result, state, "unknown exception");
			}
			if (SUCCEEDED(com_result)) CoUninitialize();
		}

		if (opt.as_json)
		{
			print_json_result(opt, result);
		}
		else
		{
			print_human_result(opt, result);
			if (result.exit_code == exit_code::usage_error) print_usage();
		}
		std::fflush(stdout);
		return result.exit_code;
	}
}
