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
// 2026/11/02 シェーダーのコンパイルとグラフィックスの初期化・終了を、DLLの InitializeEngine / ShutdownEngine と
//            同じ経路(EngineInit)で行うようにした (Primal Day 078, #31)
// 2026/11/02 エンジンのシェーダーのソースを探すルートのフォルダを --engine-path で渡せるようにした (#31 の Codex の指摘)
// 2026/11/02 TestShader.hlsl のパスを ANSI に変換せずに渡すようにした (#31 の Codex の指摘)
// 2026/11/02 エンジンのシェーダーのフォルダを extra_args の -I ではなく compile_shader() の引数で渡すようにした (#150)
// 2026/11/02 使ったD3D12のランタイム(Agility SDK か OS か、バージョン)を --json と結果に出すようにした (#155)
// 2026/11/06 遅延解放、レンダラーの設定、ジオメトリの取得と作り直し、エディタのビューポートの経路の確認を追加 (Primal Day 082, #201)
// 2026/11/06 大きさ 0 で作ったビューポートが Resize まで描画しないことの確認と、GPass のコメントの訂正 (#201)
// 2026/11/07 別のスレッドがビューポートを描く間にビューポートを操作する確認を追加 (Primal Day 083, #209)
// 2026/11/07 別のスレッドが描いている間に ShutdownEngine を呼ぶ確認を追加 (#209)
// 2026/11/07 GetDrawnFrameCount が描いたときだけ増えることの確認を追加 (#209)
// 2026/11/08 描画中にエディタがエンティティを作って消す確認を追加 (Primal Day 084, #214)
// 2026/11/08 描いたあとでメッシュをアップロードする確認を追加 (#224)
// 2026/11/08 エディタのカメラの API(UpdateEditorCamera / SetCameraRange / SetCameraFoV)の確認を追加 (Primal Day 085, #225)
// 2026/11/08 ウィンドウを持つスレッドがミューテックスを待つ間に別のスレッドが Present する確認と --show-viewports を追加 (#213)
// 2026/11/08 エディタのエンティティの記述子の回転をクォータニオンにした (#205)
// 2026/11/08 DLL の SetPosition で動かした球が新しい場所に描かれることの確認を追加 (Primal Day 086, #244)
// 2026/11/08 SetCameraFoV の確認を、視野角がラジアン(既定は 0.25 * pi)であることに合わせた (#205)
// 2026/11/09 深度プリパス用の位置だけの頂点シェーダーを登録し、マテリアルの値を詰めた形で渡す (Primal Day 087, #252)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "HeadlessRender.h"
#include "CommonHeaders.h"
#include "Core/Headless.h"
#include "Graphics/Renderer.h"
#include "Graphics/Direct3D12/D3D12Core.h"
#include "Content/ContentToEngine.h"
#include "Components/Entity.h"
#include "Components/Transform.h"
#include "Components/Geometry.h"
#include "../DXForgeEngineDLL/ShaderCompilation.h"
#include "../DXForgeEngineDLL/ResourceUpload.h"
#include "../DXForgeEngineDLL/EngineInit.h"
#include "EditorAssetPacking.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <exception>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <wincodec.h>
#include <wrl.h>

#pragma comment(lib, "windowscodecs.lib")

using namespace Microsoft::WRL;

// DXForgeEngineDLL/EngineAPI.cpp。エディタと同じ関数をビューポートの確認で呼ぶ
extern "C" void ShutdownEngine();
extern "C" u32 CreateRenderSurface(HWND host, s32 width, s32 height);
extern "C" void RemoveRenderSurface(u32 id);
extern "C" intptr_t GetWindowHandle(u32 id);
extern "C" void ResizeRenderSurface(u32 id);
extern "C" void SetGeometryIds(u32 surface_id, dxforge::id::id_type* geometry_ids, u32 count);
extern "C" void RenderFrame(u32 surface_id, dxforge::id::id_type camera_id, u64 light_set);
extern "C" u64 GetDrawnFrameCount(u32 surface_id);
extern "C" void UpdateEditorCamera(u32 surface_id, f32 pos_x, f32 pos_y, f32 pos_z, f32 rot_x, f32 rot_y, f32 rot_z);
extern "C" void SetCameraRange(u32 surface_id, f32 near_z, f32 far_z);
extern "C" void SetCameraFoV(u32 surface_id, f32 fov);
// テスト専用(DLL からはエクスポートしない)。ビューポートのカメラ。なければ無効なカメラ
dxforge::graphics::camera get_viewport_camera(u32 surface_id);
// NOTE: 引数は DXForgeEngineDLL/EntityAPI.cpp の game_entity_descriptor と同じ並びの editor_entity_descriptor
struct editor_entity_descriptor;
extern "C" dxforge::id::id_type CreateGameEntity(editor_entity_descriptor* e);
extern "C" void RemoveGameEntity(dxforge::id::id_type id);
extern "C" dxforge::id::id_type GetComponentId(dxforge::id::id_type entity_id, dxforge::component_type::type type);
extern "C" u32 SetPosition(const dxforge::id::id_type* ids, const f32* x, const f32* y, const f32* z, u32 count, u32 frame);

// DXForgeEngineDLL/EntityAPI.cpp の game_entity_descriptor と同じ並び(Tests_EditorAPI.cpp の test_descriptor と同じ)
struct editor_entity_descriptor
{
	f32 position[3]{};
	f32 rotation[4]{ 0.f, 0.f, 0.f, 1.f };	// クォータニオン(#205)
	f32 scale[3]{ 1.f, 1.f, 1.f };
	dxforge::script::detail::script_creator script_creator{ nullptr };
	dxforge::id::id_type geometry_content_id{ dxforge::id::invalid_id };
	u32 material_count{ 0 };
	dxforge::id::id_type* material_ids{ nullptr };
};

namespace dxforge::headless::render
{
	namespace
	{
		constexpr u32 min_surface_size{ graphics::min_offscreen_surface_size };	// ライトカリングのタイルの大きさ(16)
		constexpr u32 max_surface_size{ 16384 };	// D3D12のテクスチャの最大の幅・高さ

		// 頂点の要素の種類(ContentTools/Geometry.h の elements::elements_type::position_only、static_normal と同じ値)
		constexpr u32 elements_type_position_only{ 0x00 };
		constexpr u32 elements_type_static_normal{ 0x01 };

		// 引数の解析結果
		struct options
		{
			std::wstring	screenshot{};
			std::wstring	engine_path{};		// エンジン(リポジトリ)のルート。空なら作業フォルダ(実行ファイルのフォルダ)の ..\..\ を使う
			u32				frames{ 0 };
			u32				width{ 0 };
			u32				height{ 0 };
			graphics::adapter_type::type adapter{ graphics::adapter_type::warp };
			bool			has_frames{ false };
			bool			has_width{ false };
			bool			has_height{ false };
			bool			as_json{ false };
			bool			show_viewports{ false };	// #213 の確認の親ウィンドウを画面に出す
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
			// 使ったD3D12のランタイム(#155)。グラフィックスを初期化できたときだけ runtime_known が true
			bool			runtime_known{ false };
			bool			agility_sdk{ false };
			u32				sdk_version{ 0 };
			std::string		core_version{};
			std::string		core_path{};
			std::string		fallback_reason{};
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
			HWND		viewport_host{ nullptr };		// ビューポートの確認で作った親ウィンドウ
			u32			live_viewport{ u32_invalid_id };	// ShutdownEngine のあとで消すビューポート
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
				else if (arg == L"--show-viewports")
				{
					opt.show_viewports = true;
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
				else if (arg == L"--engine-path")
				{
					if (!has_value) return "--engine-path requires a value";
					opt.engine_path = argv[++i];
					if (opt.engine_path.empty()) return "--engine-path must not be empty";
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
			/// @param engine_root エンジン(リポジトリ)のルート。TestShader.hlsl とエンジンのシェーダー(-I)をここから探す
			/// @return 失敗したときはその説明(成功なら空)
			std::string create_shaders(const std::filesystem::path& engine_root)
			{
				// NOTE: .string() で ANSI に変換すると、ACP で表せない文字を含むパスでコンパイルできない
				const std::filesystem::path shader_path{ engine_root / L"EngineTest" / L"" };
				shader_file_info info{};
				info.file_name = "TestShader.hlsl";
				info.function = "TestShaderVS";
				info.type = graphics::shader_type::vertex;
				// NOTE: エンジンのシェーダーのフォルダは引数で渡す。extra_args の -I にすると、既定のフォルダ(実行ファイルの
				//		 ..\..\)より後になり、そこにある別のエンジンのヘッダーが優先される(#150)
				const std::filesystem::path include_folder{ editor::engine_shaders_source_folder(engine_root) };
				utl::vector<std::wstring> extra_args{};
				extra_args.emplace_back(L"-D");
				extra_args.emplace_back(L"ELEMENTS_TYPE=1");
				const std::unique_ptr<u8[]> vertex_shader{ compile_shader(info, shader_path, extra_args, false, include_folder) };
				// 深度プリパスは位置だけ(キー0)の版を使う(Primal Day 087)
				extra_args.back() = L"ELEMENTS_TYPE=0";
				const std::unique_ptr<u8[]> position_only_vertex_shader{ compile_shader(info, shader_path, extra_args, false, include_folder) };

				info.function = "TestShaderPS";
				info.type = graphics::shader_type::pixel;
				extra_args.clear();
				const std::unique_ptr<u8[]> pixel_shader{ compile_shader(info, shader_path, extra_args, false, include_folder) };
				if (!vertex_shader || !position_only_vertex_shader || !pixel_shader) return "failed to compile EngineTest/TestShader.hlsl";

				const u8* vertex_shaders[]{ position_only_vertex_shader.get(), vertex_shader.get() };
				const u32 vertex_keys[]{ elements_type_position_only, elements_type_static_normal };
				_vs_id = content::add_shader_group(vertex_shaders, _countof(vertex_shaders), vertex_keys);
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

			/// @brief 作成後のエンティティを DLL の SetPosition(エディタのインスペクタの経路、Primal Day 086)で動かすと、描かれる場所が変わることを確かめる(#244)
			/// @details 球を画面の右へ動かして描き、基準と違う画素が画面の右に寄ることを見る。元の位置に戻して描くと、基準と同じ画像になる。
			///			 カメラは +Z を向く右手系なので、ワールドの -X が画面の右になる。
			/// @param baseline 球が原点にあるときに読み戻した画素
			/// @return 失敗したときはその説明(成功なら空)
			std::string check_entity_moves(const graphics::surface& surface, u32 width, u32 frames, const utl::vector<math::v4>& baseline)
			{
				const auto set_sphere_x = [this](f32 x)
				{
					const id::id_type ids[]{ _sphere_entity.get_id() };
					const f32 px[]{ x }, py[]{ 0.f }, pz[]{ 0.f };
					return SetPosition(ids, px, py, pz, 1, transform::space::absolute) == 1;
				};
				const auto render_and_read = [&](utl::vector<math::v4>& pixels)
				{
					for (u32 i{ 0 }; i < frames; ++i) surface.render(frame_info());
					return surface.read_back(pixels);
				};
				const auto differs = [](const math::v4& a, const math::v4& b)
				{
					return std::fabs(a.x - b.x) > 1e-3f || std::fabs(a.y - b.y) > 1e-3f || std::fabs(a.z - b.z) > 1e-3f;
				};

				if (!set_sphere_x(-1.f)) return "SetPosition() did not accept the sphere entity";
				utl::vector<math::v4> moved{};
				if (!render_and_read(moved) || moved.size() != baseline.size()) return "failed to read back the image with the moved sphere";
				u64 changed{ 0 }, changed_on_right{ 0 };
				for (u32 i{ 0 }; i < (u32)moved.size(); ++i)
				{
					if (!differs(moved[i], baseline[i])) continue;
					++changed;
					if (i % width >= width / 2) ++changed_on_right;
				}
				if (changed == 0) return "moving the sphere with SetPosition() did not change the image";
				// 元の場所(中央)から消えた画素と、新しい場所(右)に描かれた画素なので、右半分のほうが多い
				if (changed_on_right * 2 <= changed) return "the sphere moved with SetPosition() was not drawn on the right of the image";

				if (!set_sphere_x(0.f)) return "SetPosition() did not accept the sphere entity";
				utl::vector<math::v4> restored{};
				if (!render_and_read(restored) || restored.size() != baseline.size()) return "failed to read back the image with the restored sphere";
				for (u32 i{ 0 }; i < (u32)restored.size(); ++i)
				{
					if (differs(restored[i], baseline[i])) return "moving the sphere back with SetPosition() did not restore the image";
				}
				std::fprintf(stderr, "[render] SetPosition check (#244): %llu of %u pixels changed, %llu on the right half\n",
					(unsigned long long)changed, (u32)moved.size(), (unsigned long long)changed_on_right);
				return {};
			}

			/// @brief Geometryコンポーネントのあるエンティティから、IDを指定した取得と作り直しを確かめ、描画する(Primal Day 082)
			/// @return 失敗したときはその説明(成功なら空)
			std::string check_geometry_updates(const graphics::surface& surface)
			{
				const id::id_type geometry_ids[2]{ _sphere_entity.geometry().get_id(), _floor_entity.geometry().get_id() };
				id::id_type item_ids[2]{};
				geometry::get_render_item_ids(geometry_ids, item_ids, 2);
				if (item_ids[0] != _render_item_ids[0] || item_ids[1] != _render_item_ids[1]) return "get_render_item_ids(geometry_ids) returned other items";
				game_entity::entity_id entity_ids[2]{};
				geometry::get_entity_ids(geometry_ids, entity_ids, 2);
				if (entity_ids[0] != _sphere_entity.get_id() || entity_ids[1] != _floor_entity.get_id()) return "get_entity_ids() returned other entities";

				// 床のジオメトリを作り直す
				id::id_type material_ids[]{ _floor_mtl_id };
				geometry::init_info geometry_info{};
				geometry_info.geometry_content_id = _floor_model_id;
				geometry_info.material_count = 1;
				geometry_info.material_ids = &material_ids[0];
				game_entity::entity_info info{};
				info.geometry = &geometry_info;
				if (!game_entity::update_component(_floor_entity.get_id(), info, component_type::geometry)) return "update_component(geometry) failed";
				const id::id_type floor_geometry_id{ _floor_entity.geometry().get_id() };
				if (!id::is_valid(floor_geometry_id)) return "update_component(geometry) left no component";
				geometry::get_render_item_ids(&floor_geometry_id, &_render_item_ids[1], 1);
				surface.render(frame_info());

				// 床のジオメトリを削除し、描画アイテムを減らして描く
				info.geometry = nullptr;
				if (game_entity::update_component(_floor_entity.get_id(), info, component_type::geometry)) return "update_component(geometry) created a component without init_info";
				if (id::is_valid(_floor_entity.geometry().get_id())) return "update_component(geometry) did not remove the component";
				_render_item_ids.resize(1);
				_thresholds.resize(1);
				surface.render(frame_info());
				return {};
			}

			[[nodiscard]] id::id_type sphere_geometry_id() const { return _sphere_entity.geometry().get_id(); }

			/// @brief 球のメッシュとマテリアル(check_editor_viewports がエディタと同じ経路でエンティティを作るのに使う)
			[[nodiscard]] id::id_type sphere_model_id() const { return _sphere_model_id; }
			[[nodiscard]] id::id_type sphere_material_id() const { return _sphere_mtl_id; }

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
			// NOTE: 0 はエディタのビューポートの仮のライト(EngineAPI.cpp)が使うので、ビューポートの確認と重ならない値にする
			constexpr static u64 light_set_key{ 1 };

			/// @brief エディタの AppliedMaterial.PackForEngine() の形のデータから、DLLと同じ経路でマテリアルを作る
			id::id_type create_material(math::v4 base_color, f32 metallic, f32 roughness, const id::id_type* texture_ids = nullptr, u32 texture_count = 0)
			{
				using graphics::material_surface;
				material_surface surface{};
				surface.base_color[0] = material_surface::to_unorm8(base_color.x);
				surface.base_color[1] = material_surface::to_unorm8(base_color.y);
				surface.base_color[2] = material_surface::to_unorm8(base_color.z);
				surface.base_color[3] = material_surface::to_unorm8(base_color.w);
				surface.metallic = material_surface::to_unorm8(metallic);
				surface.roughness = material_surface::to_unorm8(roughness);
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

		/// @brief Release を数えるだけの COM オブジェクト(遅延解放の確認用)
		class counting_unknown : public IUnknown
		{
		public:
			HRESULT STDMETHODCALLTYPE QueryInterface(REFIID, void** object) override { *object = nullptr; return E_NOINTERFACE; }
			ULONG STDMETHODCALLTYPE AddRef() override { return 1; }
			ULONG STDMETHODCALLTYPE Release() override { ++release_count; return 0; }
			u32 release_count{ 0 };
		};

		/// @brief 遅延解放が、前のフレームが終わるまで待つことを確かめる(Primal Day 082 の修正)
		/// @details 修正前は、積んだ次のフレームの開始で解放していた(GPU が前のフレームでまだ使っているかもしれない)
		/// @return 失敗したときはその説明(成功なら空)
		std::string check_deferred_release(const graphics::surface& surface, test_scene& scene)
		{
			static counting_unknown resource{};	// NOTE: 確認に失敗してもリストに残るので、shutdown() まで生かす
			resource.release_count = 0;
			IUnknown* ptr{ &resource };
			graphics::d3d12::core::deferred_release(ptr);
			surface.render(scene.frame_info());
			if (resource.release_count != 0) return "a deferred release ran on the next frame while earlier frames may still use the resource";
			for (u32 i{ 0 }; i < graphics::d3d12::frame_buffer_count; ++i) surface.render(scene.frame_info());
			if (resource.release_count != 1) return "a deferred release did not run after waiting for the frame buffers (" + std::to_string(resource.release_count) + " releases)";
			return {};
		}

		/// @brief レンダラーの設定(renderer_option)を確かめる
		/// @return 失敗したときはその説明(成功なら空)
		std::string check_renderer_options()
		{
			using graphics::renderer_option;
			bool vsync{ false };
			graphics::get_option(renderer_option::vsync, &vsync, sizeof(bool));
			if (!vsync) return "vsync should be enabled by default";
			vsync = false;
			graphics::set_option(renderer_option::vsync, &vsync, sizeof(bool));
			graphics::get_option(renderer_option::vsync, &vsync, sizeof(bool));
			if (vsync == graphics::d3d12::core::allow_tearing()) return "vsync can be disabled only when tearing is supported";
			vsync = true;
			graphics::set_option(renderer_option::vsync, &vsync, sizeof(bool));

			u32 samples{ 3 };
			graphics::set_option(renderer_option::msaa, &samples, sizeof(u32));
			graphics::get_option(renderer_option::msaa, &samples, sizeof(u32));
			if (samples != 1) return "msaa should ignore an unsupported sample count";
			samples = 4;
			graphics::set_option(renderer_option::msaa, &samples, sizeof(u32));
			graphics::get_option(renderer_option::msaa, &samples, sizeof(u32));
			if (samples != 4) return "msaa should accept 4 samples";
			samples = 1;
			graphics::set_option(renderer_option::msaa, &samples, sizeof(u32));

			bool raytracing{ true };
			graphics::set_option(renderer_option::raytracing, &raytracing, sizeof(bool));
			raytracing = false;
			graphics::get_option(renderer_option::raytracing, &raytracing, sizeof(bool));
			if (!raytracing) return "raytracing option was not stored";
			raytracing = false;
			graphics::set_option(renderer_option::raytracing, &raytracing, sizeof(bool));
			return {};
		}

		/// @brief サーフェスを描いたあとで、エディタと同じ経路(DLL の CreateResource)でメッシュをアップロードして描く(#224)
		/// @details 描画のコマンドが GPU に積まれたあとのアップロードで、デバイスが消えないことを確かめる。
		///			 修正前は WARP の 160x90 で、アップロードの直後に DXGI_ERROR_DEVICE_REMOVED になった。
		/// @return 失敗したときはその説明(成功なら空)
		std::string check_upload_after_render(const graphics::surface& surface, test_scene& scene)
		{
			const std::vector<u8> mesh_blob{ pack_mesh(make_sphere(1.f, 48, 24)) };
			for (u32 i{ 0 }; i < 4; ++i)
			{
				surface.render(scene.frame_info());
				const id::id_type mesh_id{ editor::create_resource(mesh_blob.data(), content::asset_type::mesh) };
				if (!id::is_valid(mesh_id)) return "failed to upload a mesh after rendering (round " + std::to_string(i + 1) + ")";
				surface.render(scene.frame_info());
				editor::destroy_resource(mesh_id, content::asset_type::mesh);
			}
			surface.render(scene.frame_info());
			return {};
		}

		/// @brief 描画中に、エディタと同じ経路でエンティティを作って消す(Primal Day 084, #214)
		/// @details render thread が viewport を描き続ける間に、このスレッドがエディタの順序(CreateGameEntity → SetGeometryIds →
		///			 SetGeometryIds で外す → RemoveGameEntity)を繰り返す。半分の回は外さずに消す(順序の誤り)。DLL の安全策
		///			 (remove_geometry_from_viewports)がないと、次の RenderFrame が消したジオメトリを読んで assert に当たる。
		///			 異常終了しないことと、デバッグレイヤーのエラーがないこと(呼び出し元の check_debug_messages)を見る。
		/// NOTE: メッシュとマテリアルは、サーフェスを作る前に作ったシーンのものを使い回し、描画中には CreateResource を呼ばない。
		///		  ビューポートのサーフェスを作って描いたあとでメッシュをアップロードすると、WARP でデバイスが消えることがある
		///		  (DXGI_ERROR_DEVICE_REMOVED。並行の問題ではなく、同じスレッドで順に呼んでも起きる。#214 の PR に書いた別の不具合)。
		/// @return 失敗したときはその説明(成功なら空)
		std::string check_editor_entities_while_rendering(test_scene& scene, u32 viewport)
		{
			std::atomic<bool> done{ false };
			std::thread render_thread{ [&]()
			{
				while (!done) RenderFrame(viewport, id::invalid_id, 0);
			} };

			std::string error{};
			const id::id_type sphere_geometry_id{ scene.sphere_geometry_id() };
			id::id_type material_id{ scene.sphere_material_id() };
			for (u32 i{ 0 }; i < 16 && error.empty(); ++i)
			{
				editor_entity_descriptor desc{};
				desc.position[0] = (f32)i * 0.1f;
				desc.geometry_content_id = scene.sphere_model_id();
				desc.material_count = 1;
				desc.material_ids = &material_id;
				const id::id_type entity_id{ CreateGameEntity(&desc) };
				const id::id_type geometry_id{ id::is_valid(entity_id) ? GetComponentId(entity_id, component_type::geometry) : id::invalid_id };
				if (!id::is_valid(geometry_id))
				{
					error = "failed to create an entity with a geometry from the editor's data";
				}
				else
				{
					id::id_type ids[2]{ sphere_geometry_id, geometry_id };
					SetGeometryIds(viewport, ids, 2);
					// 新しいエンティティを含むフレームを、少なくとも 1 回描かせる
					const u64 drawn{ GetDrawnFrameCount(viewport) };
					const u64 start{ GetTickCount64() };
					while (GetDrawnFrameCount(viewport) == drawn && GetTickCount64() - start < 5000) std::this_thread::yield();
					if (GetDrawnFrameCount(viewport) == drawn) error = "the render thread drew no frame with the new entity";
					// 偶数の回はエディタの順序(先にビューポートから外す)。奇数の回は外さずに消す(DLL の安全策が取り除く)
					if (i % 2 == 0) SetGeometryIds(viewport, ids, 1);
				}
				if (id::is_valid(entity_id)) RemoveGameEntity(entity_id);
			}

			done = true;
			render_thread.join();
			if (!error.empty()) return error;
			// 消したジオメトリが残っていれば、ここで assert に当たる
			RenderFrame(viewport, id::invalid_id, 0);
			return {};
		}

		/// @brief エディタのビューポートの経路(DLL の関数)を、隠しウィンドウと WARP で確かめる(Primal Day 082)
		/// @details 描画結果は読み戻せない(スワップチェイン)ので、異常終了しないことと、デバッグレイヤーのエラーがないことを見る。
		///			 最後のビューポートは残し、ShutdownEngine のあとで RemoveRenderSurface を呼ぶ(エディタを閉じるときの順序)。
		/// @return 失敗したときはその説明(成功なら空)
		std::string check_editor_viewports(test_scene& scene, run_state& state)
		{
			state.viewport_host = CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
			if (!state.viewport_host) return "failed to create the hidden host window";

			const auto resize = [](u32 id, int width, int height)
			{
				SetWindowPos((HWND)GetWindowHandle(id), nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
				ResizeRenderSurface(id);
			};

			// 1 回の RenderFrame で、描いた回数の累計がいくつ増えるか
			const auto drawn_by_render = [](u32 id)
			{
				const u64 before{ GetDrawnFrameCount(id) };
				RenderFrame(id, id::invalid_id, 0);
				return GetDrawnFrameCount(id) - before;
			};

			const u32 viewport{ CreateRenderSurface(state.viewport_host, 64, 64) };
			if (!GetWindowHandle(viewport)) return "CreateRenderSurface() made no window";
			if (GetDrawnFrameCount(viewport) != 0) return "a new viewport should have drawn no frames";
			if (GetDrawnFrameCount(viewport + 1000) != 0) return "GetDrawnFrameCount() should return 0 for an unknown surface";
			if (drawn_by_render(viewport) != 1) return "an empty frame of a viewport should count as drawn";	// 描くものがないフレーム
			id::id_type geometry_ids[]{ scene.sphere_geometry_id() };
			SetGeometryIds(viewport, geometry_ids, 1);
			RenderFrame(viewport, id::invalid_id, 0);
			resize(viewport, 128, 96);
			RenderFrame(viewport, id::invalid_id, 0);
			resize(viewport, 8, 8);	// ライトカリングのタイルより小さい: 描かない
			if (drawn_by_render(viewport) != 0) return "a viewport smaller than a light culling tile should not count as drawn";
			resize(viewport, 0, 0);	// 大きさが 0: サーフェスはそのまま
			if (drawn_by_render(viewport) != 0) return "a viewport resized to 0 from a small size should not count as drawn";
			resize(viewport, 64, 64);
			if (drawn_by_render(viewport) != 1) return "a viewport should count as drawn again after it grows back";

			// 大きさ 0 で作ったビューポートは、Resize でサーフェスができる
			const u32 second{ CreateRenderSurface(state.viewport_host, 0, 0) };
			state.live_viewport = second;
			// NOTE: 描画が走るとフレーム番号が進む。サーフェスがなければ RenderFrame は何もしないので進まない
			//       (create_window() は 0 を既定の大きさにするので、作成時の判定が壊れると大きなサーフェスができて描画が走る)
			const u32 frame_before_size0{ graphics::d3d12::core::current_frame_index() };
			if (drawn_by_render(second) != 0) return "a viewport created with size 0 should not count as drawn";
			if (graphics::d3d12::core::current_frame_index() != frame_before_size0) return "a viewport created with size 0 should not render before Resize";
			resize(second, 96, 64);
			const u32 frame_before_resized{ graphics::d3d12::core::current_frame_index() };
			RenderFrame(second, id::invalid_id, 0);
			if (graphics::d3d12::core::current_frame_index() == frame_before_resized) return "a viewport created with size 0 should render after Resize";
			SetGeometryIds(second, geometry_ids, 1);
			// 大きさの違う2つのビューポートを交互に描く(GPass のバッファは、大きくなるときだけ作り直される。最大に達したあとは作り直さない)
			for (u32 i{ 0 }; i < 4; ++i)
			{
				RenderFrame(viewport, id::invalid_id, 0);
				RenderFrame(second, id::invalid_id, 0);
			}

			const std::string entity_error{ check_editor_entities_while_rendering(scene, second) };
			if (!entity_error.empty()) return entity_error;
			// エディタの描画のスレッド(Primal Day 083)と同じ使い方。別のスレッドが描く間に、このスレッドがビューポートを操作する
			// NOTE: どちらのスレッドも回数を決めてあるので必ず終わる。重なり方によらず、異常終了しなければよい
			const u32 removed_while_rendering{ CreateRenderSurface(state.viewport_host, 32, 32) };
			std::thread render_thread{ [=]()
			{
				for (u32 i{ 0 }; i < 16; ++i)
				{
					RenderFrame(viewport, id::invalid_id, 0);
					RenderFrame(second, id::invalid_id, 0);
					RenderFrame(removed_while_rendering, id::invalid_id, 0);
				}
			} };
			for (u32 i{ 0 }; i < 8; ++i)
			{
				resize(viewport, 64 + 8 * i, 64);
				SetGeometryIds(second, geometry_ids, i % 2);
				// surfaces の再確保も起こす
				RemoveRenderSurface(CreateRenderSurface(state.viewport_host, 32, 32));
				if (i == 4) RemoveRenderSurface(removed_while_rendering);
			}
			render_thread.join();
			SetGeometryIds(second, geometry_ids, 1);

			RemoveRenderSurface(viewport);
			if (drawn_by_render(viewport) != 0) return "a removed viewport should not count as drawn";
			RenderFrame(second, id::invalid_id, 0);
			// NOTE: このあとシーンを消すので、残すビューポートにはジオメトリを持たせない
			SetGeometryIds(second, nullptr, 0);
			RenderFrame(second, id::invalid_id, 0);
			return {};
		}

		/// @brief エディタのカメラの API(Primal Day 085)を、隠しウィンドウと WARP のビューポートで確かめる(#225)
		/// @details スワップチェインは読み戻せないので、カメラの値(エンティティの位置と回転、視野角、範囲、ビュー射影行列)を読み戻す。
		///			 大きさ 0 で作ったビューポート(カメラがない)で呼んでも assert に当たらず、Resize で作ったカメラに値が効くことも見る。
		///			 check_editor_viewports のあと(state.viewport_host があるとき)に呼ぶ。作ったビューポートは消して戻る。
		/// @return 失敗したときはその説明(成功なら空)
		std::string check_editor_camera(run_state& state)
		{
			if (!state.viewport_host) return "check_editor_camera() needs the hidden host window";

			const auto near_equal = [](f32 a, f32 b) { return std::fabs(a - b) <= 1e-5f; };
			const auto position_is = [&](const graphics::camera& camera, math::v3 expected)
			{
				const math::v3 p{ game_entity::entity{ game_entity::entity_id{ camera.entity_id() } }.position() };
				return near_equal(p.x, expected.x) && near_equal(p.y, expected.y) && near_equal(p.z, expected.z);
			};
			const auto rotation_is = [&](const graphics::camera& camera, math::v3 radians)
			{
				using namespace DirectX;
				math::v4 expected{};
				XMStoreFloat4(&expected, XMQuaternionRotationRollPitchYawFromVector(XMLoadFloat3(&radians)));
				const math::v4 q{ game_entity::entity{ game_entity::entity_id{ camera.entity_id() } }.rotation() };
				return near_equal(q.x, expected.x) && near_equal(q.y, expected.y) && near_equal(q.z, expected.z) && near_equal(q.w, expected.w);
			};
			const auto settings_are = [&](const graphics::camera& camera, math::v3 position, math::v3 rotation, f32 near_z, f32 far_z, f32 fov_degrees)
			{
				return position_is(camera, position) && rotation_is(camera, rotation) &&
					near_equal(camera.near_z(), near_z) && near_equal(camera.far_z(), far_z) && near_equal(camera.field_of_view(), fov_degrees * math::to_rad);
			};
			const auto resize = [](u32 id, int width, int height)
			{
				SetWindowPos((HWND)GetWindowHandle(id), nullptr, 0, 0, width, height, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
				ResizeRenderSurface(id);
			};

			// カメラがあるビューポート: 値がすぐにカメラに効き、ビュー射影行列(描画)が変わる
			const u32 viewport{ CreateRenderSurface(state.viewport_host, 64, 64) };
			const graphics::camera camera{ get_viewport_camera(viewport) };
			if (!camera.is_valid()) return "a viewport created with a size should have a camera";
			RenderFrame(viewport, id::invalid_id, 0);
			const math::m4x4 before{ camera.view_projection() };
			const math::m4x4 default_projection{ camera.projection() };
			// エディタの既定の 45 度は、エンジンの既定(0.25 * pi ラジアン)と同じ射影になる(#205)
			if (!near_equal(camera.field_of_view(), 0.25f * math::pi)) return "the default field of view should be 0.25 * pi radians";
			SetCameraFoV(viewport, 45.f);
			RenderFrame(viewport, id::invalid_id, 0);
			{
				const math::m4x4 projection{ camera.projection() };
				const f32* const a{ &projection.m[0][0] };
				const f32* const b{ &default_projection.m[0][0] };
				for (u32 i{ 0 }; i < 16; ++i)
					if (!near_equal(a[i], b[i])) return "SetCameraFoV(45 degrees) should give the default projection";
			}

			const math::v3 position{ 1.f, 2.f, 3.f };
			const math::v3 rotation{ 0.1f, -2.f, 0.f };
			UpdateEditorCamera(viewport, position.x, position.y, position.z, rotation.x, rotation.y, rotation.z);
			if (!position_is(camera, position) || !rotation_is(camera, rotation)) return "UpdateEditorCamera() should move the viewport camera";
			SetCameraRange(viewport, 0.5f, 50.f);
			if (!near_equal(camera.near_z(), 0.5f) || !near_equal(camera.far_z(), 50.f)) return "SetCameraRange() should set the range of the viewport camera";
			SetCameraFoV(viewport, 90.f);
			if (!near_equal(camera.field_of_view(), math::half_pi)) return "SetCameraFoV() should convert the field of view from degrees to radians";

			const u64 drawn{ GetDrawnFrameCount(viewport) };
			RenderFrame(viewport, id::invalid_id, 0);
			if (GetDrawnFrameCount(viewport) != drawn + 1) return "a viewport should draw after its camera settings change";
			const math::m4x4 after{ camera.view_projection() };
			if (memcmp(&before, &after, sizeof(math::m4x4)) == 0) return "the view projection of the viewport camera should change with the camera settings";

			// 大きさ 0 で作ったビューポート(カメラがない): 呼んでも何もせず、Resize で作ったカメラに最後の値が効く
			const u32 empty{ CreateRenderSurface(state.viewport_host, 0, 0) };
			if (get_viewport_camera(empty).is_valid()) return "a viewport created with size 0 should have no camera";
			UpdateEditorCamera(empty, 9.f, 9.f, 9.f, 0.f, 0.f, 0.f);
			UpdateEditorCamera(empty, -1.f, 0.5f, 4.f, -0.3f, 1.f, 0.f);
			SetCameraRange(empty, 0.25f, 200.f);
			SetCameraFoV(empty, 60.f);
			RenderFrame(empty, id::invalid_id, 0);
			if (get_viewport_camera(empty).is_valid()) return "camera settings should not create a camera for a viewport of size 0";
			resize(empty, 96, 64);
			const graphics::camera created{ get_viewport_camera(empty) };
			if (!created.is_valid()) return "a viewport created with size 0 should have a camera after Resize";
			if (!settings_are(created, { -1.f, 0.5f, 4.f }, { -0.3f, 1.f, 0.f }, 0.25f, 200.f, 60.f))
				return "the camera made by Resize should use the last camera settings";
			RenderFrame(empty, id::invalid_id, 0);
			// 幅を 0 にしてもカメラは作り直さないので、値はそのまま
			resize(empty, 0, 64);
			resize(empty, 96, 64);
			if (get_viewport_camera(empty).get_id() != created.get_id() || !settings_are(created, { -1.f, 0.5f, 4.f }, { -0.3f, 1.f, 0.f }, 0.25f, 200.f, 60.f))
				return "resizing a viewport to width 0 and back should keep its camera and settings";

			// 範囲外のビューポートと、消したビューポートでは何もしない(render thread の呼び出しが RemoveRenderSurface と前後してもよい)
			UpdateEditorCamera(empty + 1000, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
			SetCameraRange(empty + 1000, 1.f, 2.f);
			SetCameraFoV(empty + 1000, 30.f);
			UpdateEditorCamera(u32_invalid_id, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
			RemoveRenderSurface(viewport);
			UpdateEditorCamera(viewport, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
			SetCameraRange(viewport, 1.f, 2.f);
			SetCameraFoV(viewport, 30.f);
			if (get_viewport_camera(viewport).is_valid()) return "a removed viewport should have no camera";
			if (!settings_are(created, { -1.f, 0.5f, 4.f }, { -0.3f, 1.f, 0.f }, 0.25f, 200.f, 60.f))
				return "camera settings for another viewport should not change this camera";

			RemoveRenderSurface(empty);
			return {};
		}

		/// @brief ウィンドウを持つスレッドがメッセージを処理せずにミューテックスを待つ間に、別のスレッドが Present する(#213)
		/// @details エディタでは、UI スレッドがビューポートのウィンドウとスワップチェインを作り、render thread が RenderFrame の中で
		///			 ミューテックスを持ったまま Present を呼ぶ。UI スレッドは UpdateEditorCamera や ResizeRenderSurface で同じミューテックスを
		///			 スピンで待ち、その間メッセージを処理しない。Present がウィンドウのスレッドのメッセージ処理を待つなら、ここで止まる。
		///			 このスレッド(ウィンドウを持つ)は、確認の間に一度もメッセージを処理しない。止まったときは、見張りのスレッドが
		///			 時間切れでプロセスを終了コード 22 で終える(止まったスレッドは戻れないので、結果の JSON は出ない)。
		///			 UpdateEditorCamera 1 回にかかった時間(ほぼミューテックスの待ち)も測り、標準エラーに出す。
		/// @param show_window true なら親ウィンドウを画面に出す(--show-viewports。実機の GPU で DWM を通す確認用)
		/// @return 失敗したときはその説明(成功なら空)
		std::string check_present_while_window_thread_waits(test_scene& scene, bool show_window)
		{
			constexpr u32 iterations{ 300 };		// このスレッドが UpdateEditorCamera を呼ぶ回数
			constexpr u32 resize_every{ 5 };		// この回数ごとにリサイズする
			constexpr DWORD timeout_ms{ 60000 };	// これを超えたら止まったとみなす
			constexpr int width{ 320 };
			constexpr int height{ 180 };

			const DWORD style{ WS_POPUP | (show_window ? (DWORD)WS_VISIBLE : 0u) };
			const HWND host{ CreateWindowExW(0, L"STATIC", L"", style, 100, 100, width + 64, height + 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr) };
			if (!host) return "failed to create the host window for the present check";

			const HANDLE finished{ CreateEventW(nullptr, TRUE, FALSE, nullptr) };
			if (!finished)
			{
				DestroyWindow(host);
				return "failed to create the event for the present check";
			}
			std::thread watchdog{ [finished]()
			{
				if (WaitForSingleObject(finished, timeout_ms) == WAIT_TIMEOUT)
				{
					std::fprintf(stderr, "[render] present check: no progress for %lu ms; the window thread and Present are waiting on each other (#213)\n", timeout_ms);
					std::fflush(stderr);
					TerminateProcess(GetCurrentProcess(), exit_code::render_failed);
				}
			} };

			const u32 viewport{ CreateRenderSurface(host, width, height) };
			if (show_window) ShowWindow((HWND)GetWindowHandle(viewport), SW_SHOWNA);
			id::id_type geometry_ids[]{ scene.sphere_geometry_id() };
			SetGeometryIds(viewport, geometry_ids, 1);

			std::atomic<bool> stop{ false };
			u64 presented{ 0 };
			double render_ms_total{ 0.0 };
			const auto seconds_since = [](LARGE_INTEGER start)
			{
				LARGE_INTEGER now{}, frequency{};
				QueryPerformanceCounter(&now);
				QueryPerformanceFrequency(&frequency);
				return (double)(now.QuadPart - start.QuadPart) / (double)frequency.QuadPart;
			};
			std::thread render_thread{ [&]()
			{
				while (!stop.load())
				{
					LARGE_INTEGER start{};
					QueryPerformanceCounter(&start);
					RenderFrame(viewport, id::invalid_id, 0);
					render_ms_total += seconds_since(start) * 1000.0;
					++presented;
				}
			} };

			// このスレッドはメッセージを処理しない(GetMessage / PeekMessage を呼ばない)
			double wait_ms_total{ 0.0 };
			double wait_ms_max{ 0.0 };
			u32 resizes{ 0 };
			LARGE_INTEGER loop_start{};
			QueryPerformanceCounter(&loop_start);
			for (u32 i{ 0 }; i < iterations; ++i)
			{
				LARGE_INTEGER start{};
				QueryPerformanceCounter(&start);
				UpdateEditorCamera(viewport, 0.f, 0.f, 3.f + 0.001f * i, 0.f, 0.01f * i, 0.f);
				const double wait_ms{ seconds_since(start) * 1000.0 };
				wait_ms_total += wait_ms;
				if (wait_ms > wait_ms_max) wait_ms_max = wait_ms;

				if (i % resize_every == 0)
				{
					const int w{ width - 32 * (int)(resizes % 2) };
					SetWindowPos((HWND)GetWindowHandle(viewport), nullptr, 0, 0, w, height, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
					ResizeRenderSurface(viewport);
					++resizes;
				}
			}
			const double loop_s{ seconds_since(loop_start) };
			stop.store(true);
			render_thread.join();
			SetEvent(finished);
			watchdog.join();
			CloseHandle(finished);

			SetGeometryIds(viewport, nullptr, 0);
			RemoveRenderSurface(viewport);
			DestroyWindow(host);

			std::fprintf(stderr,
				"[render] present check (#213): %u UpdateEditorCamera calls and %u resizes on the window thread without pumping messages, "
				"%llu frames presented in %.2f s; wait per call avg %.3f ms, max %.3f ms; RenderFrame avg %.3f ms\n",
				iterations, resizes, (unsigned long long)presented, loop_s, wait_ms_total / iterations, wait_ms_max,
				presented ? render_ms_total / (double)presented : 0.0);
			if (presented == 0) return "the render thread presented no frames during the present check";
			return {};
		}

		/// @brief 描画して読み戻し、PNGに書き出す(graphics::initialize()のあと)
		void render_and_capture(const options& opt, const std::filesystem::path& engine_root, run_result& result, run_state& state)
		{
			// NOTE: 例外で抜けたときは、sceneとsurfaceのデストラクタが、graphics::shutdown()より先に後片付けをする
			test_scene scene{};
			state.stage = run_stage::shaders;
			const std::string shader_error{ scene.create_shaders(engine_root) };
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
			// NOTE: 読み戻したあとで、基準画像に影響しない確認をする(Primal Day 082)
			// NOTE: 床を消す check_geometry_updates より先に、基準の画像と比べる
			std::string check_error{ read ? scene.check_entity_moves(surface.get(), opt.width, opt.frames, pixels) : std::string{} };
			if (check_error.empty()) check_error = check_deferred_release(surface.get(), scene);
			if (check_error.empty()) check_error = check_renderer_options();
			if (check_error.empty()) check_error = scene.check_geometry_updates(surface.get());
			if (check_error.empty()) check_error = check_editor_viewports(scene, state);
			if (check_error.empty()) check_error = check_upload_after_render(surface.get(), scene);
			if (check_error.empty()) check_error = check_editor_camera(state);
			if (check_error.empty()) check_error = check_present_while_window_thread_waits(scene, opt.show_viewports);
			if (!check_error.empty())
			{
				fail(result, exit_code::render_failed, "render_failed", check_error);
				return;
			}
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

		/// @brief ShutdownEngine のあとで、残したビューポートのウィンドウを消す(エディタの HwndHost の順序)
		void finish_viewports(run_state& state)
		{
			if (state.live_viewport != u32_invalid_id)
			{
				ResizeRenderSurface(state.live_viewport);	// サーフェスがなければ何もしない
				RenderFrame(state.live_viewport, id::invalid_id, 0);
				RemoveRenderSurface(state.live_viewport);
				ResizeRenderSurface(state.live_viewport);	// エディタのタイマーは消したあとにも動くことがある
				state.live_viewport = u32_invalid_id;
			}
			if (state.viewport_host) DestroyWindow(state.viewport_host);
			state.viewport_host = nullptr;
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
			// エンジンのパスも、作業フォルダを移す前に絶対パスにする
			std::filesystem::path engine_root{ L"..\\..\\" };
			if (!opt.engine_path.empty())
			{
				engine_root = std::filesystem::absolute(opt.engine_path, error);
				if (error)
				{
					fail(result, exit_code::shaders_unavailable, "shaders_unavailable", "invalid engine path: " + to_utf8(opt.engine_path));
					return;
				}
			}

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
			if (!std::filesystem::is_directory(editor::engine_shaders_source_folder(engine_root), error) ||
				!std::filesystem::is_regular_file(engine_root / L"EngineTest" / L"TestShader.hlsl", error))
			{
				fail(result, exit_code::shaders_unavailable, "shaders_unavailable",
					"shader sources were not found (EngineUnitTests.exe must stay in x64\\<configuration>\\ of the repository, or pass --engine-path)");
				return;
			}
			// NOTE: エディタから初期化するときと同じ経路(DLLの InitializeEngine = editor::initialize_engine)で、
			//		 シェーダーをコンパイルしてからグラフィックスを初期化する(Primal Day 078)。
			//		 初期化に失敗したときは、D3D12の側(failed_init())でデバッグレイヤーのメッセージを出してから後片付けまで
			//		 済ませている。editor::shutdown_engine() は、そのときに graphics::shutdown() を重ねて呼ばない(#128のレビュー)。
			graphics::graphics_init_info init_info{};
			init_info.adapter = opt.adapter;
			init_info.unattended = true;
			// --engine-path がなければ、エンジンのパスを渡さない経路(作業フォルダの ..\..\)を確かめる
			const std::filesystem::path init_engine_path{ opt.engine_path.empty() ? std::filesystem::path{} : engine_root };
			const editor::engine_init_error::error_code init_result{ editor::initialize_engine(init_info, init_engine_path) };
			if (init_result != editor::engine_init_error::succeeded)
			{
				// 失敗したあとの ShutdownEngine は何もしない(エディタは失敗しても終了時に呼ぶ)
				editor::shutdown_engine();
				if (init_result == editor::engine_init_error::shader_compilation)
				{
					fail(result, exit_code::shaders_unavailable, "shaders_unavailable", "failed to compile the engine shaders");
				}
				else
				{
					fail(result, exit_code::graphics_init_failed, "graphics_init_failed",
						std::string{ "failed to initialize Direct3D 12 with the " } + adapter_label(opt.adapter) + " adapter (engine_init_error " +
						std::to_string((u32)init_result) + ")");
				}
				return;
			}
			state.stage = run_stage::graphics_init;
			state.graphics_initialized = true;
			// 初期化済みのときの InitializeEngine は、初期化し直さずに成功を返す
			if (editor::initialize_engine(init_info, init_engine_path) != editor::engine_init_error::succeeded || !editor::is_engine_initialized())
			{
				fail(result, exit_code::graphics_init_failed, "graphics_init_failed", "initialize_engine() did not succeed when already initialized");
				state.graphics_initialized = false;
				editor::shutdown_engine();
				return;
			}
			result.adapter_name = graphics::adapter_name();
			std::fprintf(stderr, "[render] adapter: %s (%s)\n", result.adapter_name.c_str(), adapter_label(opt.adapter));
			{
				const graphics::runtime_info runtime{ graphics::runtime() };
				result.runtime_known = true;
				result.agility_sdk = runtime.agility_sdk;
				result.sdk_version = runtime.sdk_version;
				result.core_version = runtime.core_version;
				result.core_path = runtime.core_path;
				result.fallback_reason = runtime.fallback_reason;
			}

			render_and_capture(opt, engine_root, result, state);

			// 後片付けで出たメッセージも出す(失敗の扱いは描画のときのものを優先する)
			state.stage = run_stage::render;
			if (result.exit_code == exit_code::success) check_debug_messages(result, "while cleaning up");
			else graphics::flush_debug_messages();
			state.graphics_initialized = false;
			// NOTE: 残したビューポートのサーフェスは ShutdownEngine が片付ける(エディタの DLL と同じ関数)
			// NOTE: エディタの render thread が残ったまま ShutdownEngine に入っても、終わったあとの RenderFrame は何もしない
			std::atomic_bool render_started{ false };
			std::atomic_bool stop_rendering{ false };
			std::thread render_thread{};
			if (state.live_viewport != u32_invalid_id)
			{
				render_thread = std::thread{ [&render_started, &stop_rendering, viewport = state.live_viewport]()
					{
						while (!stop_rendering)
						{
							RenderFrame(viewport, id::invalid_id, 0);
							render_started = true;
						}
					} };
				while (!render_started) std::this_thread::yield();
			}
			ShutdownEngine();
			stop_rendering = true;
			if (render_thread.joinable()) render_thread.join();
			finish_viewports(state);
			// 2回目の ShutdownEngine は何もしない(エディタはウィンドウを閉じるときとキャンセルのときに呼ぶ)
			ShutdownEngine();
			if (editor::is_engine_initialized() && result.exit_code == exit_code::success)
			{
				fail(result, exit_code::render_failed, "render_failed", "the engine is still initialized after shutdown_engine()");
			}
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
					ShutdownEngine();
					finish_viewports(state);
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
			if (result.runtime_known)
			{
				std::printf("\"d3d12_runtime\":{\"source\":\"%s\",\"sdk_version\":%u,\"core_version\":\"%s\",\"core_path\":\"%s\",\"fallback_reason\":",
					result.agility_sdk ? "agility" : "os", result.sdk_version, escape_json(result.core_version).c_str(), escape_json(result.core_path).c_str());
				if (result.fallback_reason.empty()) std::printf("null},");
				else std::printf("\"%s\"},", escape_json(result.fallback_reason).c_str());
			}
			else
			{
				std::printf("\"d3d12_runtime\":null,");
			}
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
			if (result.runtime_known)
			{
				std::printf("  D3D12 runtime: %s %u (D3D12Core %s)\n", result.agility_sdk ? "Agility SDK" : "OS, not the Agility SDK",
					result.sdk_version, result.core_version.empty() ? "unknown" : result.core_version.c_str());
			}
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
			"  --engine-path <dir>  root of the engine repository for the shader sources (default: ..\\..\\ of the exe folder)\n"
			"  --json               print a machine-readable result to stdout (logs go to stderr)\n"
			"  --show-viewports     show the window of the Present/window-thread check (#213) on screen (use with --adapter hardware)\n"
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
