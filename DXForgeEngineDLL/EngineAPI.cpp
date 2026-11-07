// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [EngineAPI.cpp]
// 作成日 : 2024/11/14
// 作成者 : 田中ミノル
// 概要 :
// エンジンのAPI
// 更新履歴
// 2024/11/14 新規作成
// 2026/10/27 シェーダーのコンパイルとシェーダーグループの登録・削除を追加(Primal Day 076)
// 2026/10/30 エディタからアセットのリソースを作る・破棄する CreateResource / DestroyResource を追加(Primal Day 077, #30)
// 2026/11/02 エディタからエンジンを初期化・終了する InitializeEngine / ShutdownEngine を追加(Primal Day 078, #31)
// 2026/11/02 CompileShader のインクルードのフォルダの扱いをコメントに追加(#150)
// 2026/11/02 使っている D3D12 のランタイムをエディタに返す GetGraphicsRuntimeDescription を追加(#155)
// 2026/11/06 ビューポートのカメラとライト、サーフェスの作成、SetGeometryIds、RenderFrame を追加 (Primal Day 082, #201)
// 2026/11/07 ミューテックスを ticket_mutex にし、エンティティ ID の取得を RenderFrame に移した (Primal Day 083, #209)
// 2026/11/07 ShutdownEngine のコメントをエディタの閉じる順序に合わせた (#209)
// 2026/11/07 実際に描いた回数を返す GetDrawnFrameCount を追加 (#209)
// 2026/11/08 消すジオメトリの ID をビューポートから取り除く remove_geometry_from_viewports を追加 (#214)
// 2026/11/08 エディタのカメラの UpdateEditorCamera / SetCameraRange / SetCameraFoV とマウスの追跡を追加 (Primal Day 085, #225)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Common.h"
#include "CommonHeaders.h"
#include "Components/Script.h"
#include "Components/Entity.h"
#include "Components/Geometry.h"
#include "Components/Transform.h"
#include "Graphics/Renderer.h"
#include "Platform/PlatformType.h"
#include "Platform/Platform.h"
#include "Content/ContentToEngine.h"
#include "Utilities/IOStream.h"
#include "Utilities/Threading.h"
#include "ShaderCompilation.h"
#include "ResourceUpload.h"
#include "EngineInit.h"

#ifdef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <oleauto.h>	// LPSAFEARRAY(GetScriptNames)。ATLは使わない (#146)
#include <string_view>

using namespace dxforge;

namespace
{
	HMODULE game_code_dll{ nullptr };
	using _get_script_creater = script::detail::script_creator(*)(size_t);
	_get_script_creater get_script_creator{ nullptr };
	using _get_script_names = LPSAFEARRAY(*)(void);
	_get_script_names get_script_names{ nullptr };

	// エディタが最後に設定したビューポートのカメラの値(Primal Day 085, #225)
	// NOTE: Primal にはない。DXForgeEX はカメラを大きさが 0 でないときだけ作る(create_viewport_graphics)ので、
	//       エディタが値を設定したときにカメラがまだないことがある。値を覚えておき、カメラを作ったときに反映する。
	struct editor_camera_settings
	{
		math::v3 position{};
		math::v4 rotation{};	// クォータニオン
		f32 near_z{ 0.f };
		f32 far_z{ 0.f };
		f32 field_of_view{ 0.f };	// エンジンの単位(度数 / 180)
		bool has_transform{ false };
		bool has_range{ false };
		bool has_field_of_view{ false };
	};

	// エディタのビューポート。カメラと、描くジオメトリを持つ
	struct viewport_surface : public graphics::render_surface
	{
		graphics::camera camera{};
		editor_camera_settings camera_settings{};
		utl::vector<id::id_type> geometry_ids{};
		u64 drawn_frame_count{ 0 };	// RenderFrame が実際に描いた回数(GetDrawnFrameCount)
	};

	utl::vector<viewport_surface> surfaces;
	utl::vector<u8> frame_info_buffer;
	std::wstring graphics_runtime_description{};	// GetGraphicsRuntimeDescription が返す文字列の実体

	void calculate_thresholds(const game_entity::entity_id* const entity_ids,
		f32* const thresholds, u32 count, const graphics::camera& view_camera)
	{
		const game_entity::entity camera{ game_entity::entity_id{ view_camera.entity_id() } };

		using namespace DirectX;
		math::v3 pos{ camera.position() };
		const XMVECTOR camera_pos{ XMLoadFloat3(&pos) };

		for (u32 i{ 0 }; i < count; ++i)
		{
			assert(id::is_valid(entity_ids[i]));
			pos = game_entity::entity{ entity_ids[i] }.position();
			const XMVECTOR entity_pos{ XMLoadFloat3(&pos) };
			const XMVECTOR distance{ XMVector3LengthEst(camera_pos - entity_pos) };
			XMStoreFloat(&thresholds[i], distance);
		}
	}

	// TEMPORARY: エディタにカメラとライトができるまでの仮のもの(Primal Day 082) ///////////////

	graphics::light lights[4]{};

	math::v3 rgb_to_color(u8 r, u8 g, u8 b) { return { r / 255.f, g / 255.f, b / 255.f }; }

	// NOTE: Primal の script_name と geometry_info の引数は省いた。Primal Day 082〜084 でも常に nullptr を渡すだけで使わない
	game_entity::entity create_one_game_entity(math::v3 position, math::v3 rotation)
	{
		constexpr math::v3 scale{ 1.f, 1.f, 1.f };
		transform::init_info transform_info{};
		const DirectX::XMVECTOR quat{ DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3(&rotation)) };
		math::v4a rot_quat{};
		DirectX::XMStoreFloat4A(&rot_quat, quat);
		memcpy(&transform_info.rotation[0], &rot_quat.x, sizeof(transform_info.rotation));
		memcpy(&transform_info.position[0], &position.x, sizeof(transform_info.position));
		memcpy(&transform_info.scale[0], &scale.x, sizeof(transform_info.scale));

		game_entity::entity_info entity_info{};
		entity_info.transform = &transform_info;
		const game_entity::entity ntt{ game_entity::create(entity_info) };
		assert(ntt.is_valid());
		return ntt;
	}

	graphics::camera create_camera()
	{
		const game_entity::entity ntt{ create_one_game_entity({ 0.f, 1.f, 10.f }, { 0.f, -math::pi, 0.f }) };
		return graphics::create_camera(graphics::perspective_camera_init_info{ ntt.get_id() });
	}

	// エディタが設定したカメラの値のうち、設定したものをカメラに反映する。カメラがなければ何もしない(#225)
	void apply_camera_settings(const viewport_surface& surface)
	{
		const graphics::camera& camera{ surface.camera };
		if (!camera.is_valid()) return;
		const editor_camera_settings& settings{ surface.camera_settings };

		if (settings.has_transform)
		{
			const id::id_type entity_id{ camera.entity_id() };
			assert(id::is_valid(entity_id));
			transform::component_cache cache{};
			cache.position = settings.position;
			cache.rotation = settings.rotation;
			cache.flags = transform::component_flags::position | transform::component_flags::rotation;
			cache.id = transform::transform_id{ entity_id };
			transform::update(&cache, 1);
		}
		if (settings.has_range) camera.range(settings.near_z, settings.far_z);
		if (settings.has_field_of_view) camera.field_of_view(settings.field_of_view);
	}

	// エディタのカメラの API が値を設定してよいビューポートか(#225)
	// NOTE: Primal は surface_id を assert で確かめるだけ。DXForgeEX では、render thread の EditorCamera.ChangePosition / Seek が
	//       RemoveRenderSurface と前後しうるので、範囲外と、消したあと(ウィンドウがない)のビューポートでは何もしない。
	bool is_editor_camera_target(u32 surface_id)
	{
		return surface_id < surfaces.size() && surfaces[surface_id].window.is_valid();
	}

	// マウスがウィンドウの外に出たことを知らせてもらう(Primal Day 085)。エディタの RenderSurfaceControl が
	// WM_MOUSEHOVER / WM_MOUSELEAVE で、マウスがビューポートの上にあるかを判断する。
	bool tracking{ false };
	void track_mouse(HWND hwnd)
	{
		TRACKMOUSEEVENT tme{};
		tme.cbSize = sizeof(TRACKMOUSEEVENT);
		tme.dwFlags = TME_HOVER | TME_LEAVE;
		tme.dwHoverTime = 10;
		tme.hwndTrack = hwnd;

		TrackMouseEvent(&tme);
	}

	LRESULT win_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
	{
		switch (msg)
		{
		case WM_MOUSEMOVE:
			if (!tracking)
			{
				track_mouse(hwnd);
				tracking = true;
			}
			break;
		case WM_MOUSELEAVE:
			tracking = false;
			break;
		}
		return DefWindowProc(hwnd, msg, wparam, lparam);
	}

	void remove_camera(graphics::camera& camera)
	{
		const game_entity::entity_id id{ camera.entity_id() };
		graphics::remove_camera(camera.get_id());
		camera = {};
		game_entity::remove(id);
	}

	void create_lights()
	{
		graphics::create_light_set(0);

		graphics::light_init_info info{};
		info.entity_id = create_one_game_entity({}, { 0.23f, 5.28f, 0.f }).get_id();
		info.type = graphics::light::directional;
		info.light_set_key = 0;
		info.intensity = 0.5f;
		info.color = rgb_to_color(174, 174, 174);
		lights[0] = graphics::create_light(info);

		info.entity_id = create_one_game_entity({}, { 0.23f, 5.28f - math::pi, 0.f }).get_id();
		info.intensity = 1.f;
		lights[1] = graphics::create_light(info);

		info.intensity = 0.5f;

		info.entity_id = create_one_game_entity({}, { math::pi * 0.5f, 0.f, 0.f }).get_id();
		info.color = rgb_to_color(17, 27, 48);
		lights[2] = graphics::create_light(info);

		info.entity_id = create_one_game_entity({}, { -math::pi * 0.5f, 0.f, 0.f }).get_id();
		info.color = rgb_to_color(63, 47, 30);
		lights[3] = graphics::create_light(info);
	}

	void remove_lights()
	{
		for (graphics::light& light : lights)
		{
			if (!light.is_valid()) continue;
			const game_entity::entity_id id{ light.entity_id() };
			graphics::remove_light(light.get_id(), light.light_set_key());
			light = {};
			game_entity::remove(id);
		}

		graphics::remove_light_set(0);
	}

	// TEMPORARY ここまで ///////////////////////////////////////////////////////////////////////

	// ビューポートのサーフェスとカメラを作る。ライトは最初のビューポートで作る(Primal Day 083 の条件の修正を先に入れた)
	// NOTE: エンジンの初期化に失敗したあと(DXGI のファクトリがない)と、大きさが 0 のときは作らない。大きさが 0 のときは次の Resize で作る。
	void create_viewport_graphics(viewport_surface& surface)
	{
		const u32 width{ surface.window.width() };
		const u32 height{ surface.window.height() };
		if (!editor::is_engine_initialized() || !width || !height) return;

		if (!lights[0].is_valid()) create_lights();
		surface.surface = graphics::create_surface(surface.window);
		surface.camera = create_camera();
		surface.camera.aspect_ratio((f32)width / height);
		apply_camera_settings(surface);	// カメラを作る前にエディタが設定した値(#225)
	}

	void remove_viewport_graphics(viewport_surface& surface)
	{
		if (surface.camera.is_valid()) remove_camera(surface.camera);
		if (surface.surface.is_valid()) graphics::remove_surface(surface.surface.get_id());
		surface.surface = {};
	}
} // 匿名名前空間

extern utl::ticket_mutex mutex;	// EntityAPI.cpp
math::v4 to_quat(math::v3 angles, bool is_degrees);	// EntityAPI.cpp

// ビューポートのカメラ(テスト専用。EngineUnitTests が extern で宣言する。DLL からはエクスポートしない、#225)
// 範囲外の surface_id やカメラがないときは無効なカメラを返す。mutex を持たないので、描画と同時には呼ばない。
graphics::camera get_viewport_camera(u32 surface_id)
{
	return surface_id < surfaces.size() ? surfaces[surface_id].camera : graphics::camera{};
}

// 消す前のジオメトリのコンポーネントの ID を、全ビューポートの描くジオメトリから取り除く(#214)。
// EntityAPI.cpp の RemoveGameEntity と UpdateComponent が、mutex を持ったまま、エンジンから消す前に呼ぶ。
// NOTE: Primal にはない DXForgeEX の安全策。エディタは消す前に SetGeometryIds で外す(Primal Day 084 の順序)ので、
//       ふつうは何もしない。順序を誤ると、消した ID が残り、次の RenderFrame の geometry::get_entity_ids が assert に当たる。
//       順序の誤りが隠れないよう、取り除いたときは Debug でデバッグ出力に 1 行出す(assert にはしない)。
void remove_geometry_from_viewports(id::id_type geometry_id)
{
	if (!id::is_valid(geometry_id)) return;

	for (u32 surface_id{ 0 }; surface_id < surfaces.size(); ++surface_id)
	{
		utl::vector<id::id_type>& ids{ surfaces[surface_id].geometry_ids };
		for (u64 i{ 0 }; i < ids.size();)
		{
			if (ids[i] == geometry_id)
			{
				ids.erase(i);
#ifdef _DEBUG
				wchar_t message[160];
				swprintf_s(message, L"[DXForgeEngineDLL] removed geometry %u from viewport %u: the editor removed it before SetGeometryIds (#214)\n", geometry_id, surface_id);
				OutputDebugStringW(message);
#endif
			}
			else
			{
				++i;
			}
		}
	}
}

// エンジンのシェーダーをコンパイルし、グラフィックスを初期化する(Primal Day 078)。返す値は editor::engine_init_error。
// engine_path: エンジン(リポジトリ)のルートのフォルダ(UTF-16)。シェーダーのソースはその DXForgeEngine\Graphics\Direct3D12\Shaders から
//              探す。nullptr か空なら、作業フォルダの ..\..\ をルートとみなす。shaders.bin は作業フォルダの shaders\d3d12\ に置く。
// NOTE: Primal の InitializeEngine() は引数を取らず、作業フォルダからの相対パスでソースを探す。DXForgeEX のエディタは
//       リポジトリの外に置いて DXFORGE_ENGINE などでエンジンのパスを決めて起動できる(#117)ので、そのパスを渡す(#31)。
// NOTE: Primal はシェーダーのコンパイルに失敗すると、ここで MessageBox(MB_RETRYCANCEL)を出して再試行する。
//       ヘッドレスのテストで止まらないよう、ここでは UI を出さず、再試行はエディタ(MainWindow)が聞く。
// NOTE: D3D12 のデバッグレイヤーは、デバッガがつながっているときだけブレークさせる(Primal は常にブレーク)。
//       デバッガなしで DebugEditor を起動したとき、警告1つでエディタが落ちないようにするため。
//       メッセージはどちらでもデバッグレイヤーが OutputDebugString に出す。
EDITOR_INTERFACE u32 InitializeEngine(const wchar_t* engine_path)
{
	graphics::graphics_init_info info{};
	info.unattended = !IsDebuggerPresent();
	return editor::initialize_engine(info, (engine_path && *engine_path) ? std::filesystem::path{ engine_path } : std::filesystem::path{});
}

// InitializeEngine で初期化したグラフィックスを終了する。初期化していないとき、2回目は何もしない。
// NOTE: エディタは、ふつうは RemoveRenderSurface のあとで ShutdownEngine を呼ぶ(Primal Day 083 の RenderSurfaceControl、#209)。
//       host が消されないまま終わると、ShutdownEngine のあとで HwndHost が RemoveRenderSurface を呼ぶ。
//       終了した D3D12 を触らないよう、残っているサーフェスとカメラはここで片付ける(ウィンドウは RemoveRenderSurface で消す)。
EDITOR_INTERFACE void ShutdownEngine()
{
	std::lock_guard lock{ mutex };
	for (viewport_surface& surface : surfaces)
	{
		remove_viewport_graphics(surface);
	}
	// TEMPORARY
	if (lights[0].is_valid()) remove_lights();
	editor::shutdown_engine();
}

// 使っている D3D12 のランタイム(Agility SDK か OS か、バージョンとパス)の説明(UTF-16、#155)。InitializeEngine のあとで呼ぶ。
// agility_sdk には Agility SDK を使っているとき 1、OS のランタイムなら 0 を入れる(nullptr でもよい)。
// 返す文字列は、次にこの関数を呼ぶまで使える。初期化前は空。
EDITOR_INTERFACE const wchar_t* GetGraphicsRuntimeDescription(u32* agility_sdk)
{
	const graphics::runtime_info info{ graphics::runtime() };
	if (agility_sdk) *agility_sdk = info.agility_sdk ? 1 : 0;
	const auto to_wide = [](const char* text) {
		const int size{ MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0) };
		std::wstring wide(size > 0 ? (size_t)size : 0, L'\0');
		if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text, -1, wide.data(), size);
		if (!wide.empty()) wide.pop_back();	// 終端の L'\0'
		return wide;
	};
	graphics_runtime_description.clear();
	if (!info.sdk_version) return graphics_runtime_description.c_str();
	const std::wstring core{ L"D3D12Core " + (*info.core_version ? to_wide(info.core_version) : std::wstring{ L"unknown" }) + L", " + to_wide(info.core_path) };
	if (info.agility_sdk)
	{
		graphics_runtime_description = L"Agility SDK " + std::to_wstring(info.sdk_version) + L" (" + core + L")";
	}
	else
	{
		graphics_runtime_description = L"OS (" + core + L"); Agility SDK " + std::to_wstring(info.sdk_version) + L" is not used: " + to_wide(info.fallback_reason);
	}
	return graphics_runtime_description.c_str();
}

EDITOR_INTERFACE u32 LoadGameCodeDll(const char* dll_path)
{
	if (game_code_dll) return FALSE;
	game_code_dll = LoadLibraryA(dll_path);
	assert(game_code_dll);

	get_script_creator = (_get_script_creater)GetProcAddress(game_code_dll, "get_script_creator");
	get_script_names = (_get_script_names)GetProcAddress(game_code_dll, "get_script_names");

	return (game_code_dll && get_script_creator && get_script_names) ? TRUE : FALSE;
}

EDITOR_INTERFACE u32 UnloadGameCodeDll()
{
	if (!game_code_dll) return FALSE;
	assert(game_code_dll);
	[[maybe_unused]] int result{ FreeLibrary(game_code_dll) };
	assert(result);
	game_code_dll = nullptr;
	return TRUE;
}

EDITOR_INTERFACE script::detail::script_creator GetScriptCreater(const char* name)
{
	return(game_code_dll && get_script_creator) ? get_script_creator(script::detail::string_hash()(name)) : nullptr;
}

EDITOR_INTERFACE LPSAFEARRAY GetScriptNames()
{
	return (game_code_dll && get_script_names) ? get_script_names() : nullptr;
};

EDITOR_INTERFACE u32 CreateRenderSurface(HWND host, s32 width, s32 height)
{
	std::lock_guard lock{ mutex };
	assert(host);
	platform::window_init_info info{ &win_proc,host,nullptr,0,0,(u32)width,(u32)height };
	viewport_surface surface{};
	surface.window = platform::create_window(&info);
	assert(surface.window.is_valid());
	// NOTE: create_window() は 0 を既定の大きさとして扱うので、大きさが 0 かどうかは引数で見る(次の Resize で作る)
	if (width > 0 && height > 0) create_viewport_graphics(surface);
	surfaces.emplace_back(surface);
	return (u32)surfaces.size() - 1;
}

EDITOR_INTERFACE void RemoveRenderSurface(u32 id)
{
	std::lock_guard lock{ mutex };
	assert(id < surfaces.size());
	viewport_surface& surface{ surfaces[id] };
	remove_viewport_graphics(surface);
	if (surface.window.is_valid()) platform::remove_window(surface.window.get_id());
	surface.window = {};
	surface.geometry_ids.clear();
}

EDITOR_INTERFACE intptr_t GetWindowHandle(u32 id)
{
	std::lock_guard lock{ mutex };
	assert(id < surfaces.size());
	return reinterpret_cast<intptr_t>(surfaces[id].window.handle());
}

EDITOR_INTERFACE void ResizeRenderSurface(u32 id)
{
	std::lock_guard lock{ mutex };
	// NOTE: エディタのリサイズのタイマーは、サーフェスを消したあとにも動くことがある
	if (id >= surfaces.size() || !surfaces[id].window.is_valid()) return;
	viewport_surface& surface{ surfaces[id] };
	surface.window.resize(0, 0);
	const u32 width{ surface.window.width() };
	const u32 height{ surface.window.height() };
	if (!width || !height) return;	// 大きさが 0 の間はそのままにして、次の Resize で合わせる

	if (!surface.surface.is_valid())
	{
		create_viewport_graphics(surface);
		return;
	}
	surface.surface.resize(width, height);
	surface.camera.aspect_ratio((f32)width / height);
}

// エディタのアセットの PackForEngine() のデータから、エンジンのリソースを作る(Primal Day 077)。
// type は content::asset_type::type(エディタの AssetType と同じ値)。material、mesh、texture だけを扱う。
// 作れなければ id::invalid_id。マテリアルのデータの並びは ResourceUpload.h を参照。
// NOTE: Primal Day 077 は id::invalid_id を返すだけで、content::create_resource を呼ぶのは Day 078 から。
//       テストで確かめられるよう、#30 で Day 078 の形まで入れた。InitializeEngine のあとで呼ぶ。
EDITOR_INTERFACE id::id_type CreateResource(const u8* data, u32 type)
{
	return editor::create_resource(data, type);
}

EDITOR_INTERFACE void DestroyResource(id::id_type id, u32 type)
{
	editor::destroy_resource(id, type);
}
namespace
{
	// エディタの EngineAPIStructs.ShaderData と同じ並び
	struct shader_data
	{
		u32 type;
		u32 code_size;
		u32 byte_code_size;
		u32 errors_size;
		u32 assembly_size;
		u32 hash_size;
		u8* code;
		u8* byte_code_error_assembly_hash;
		const char* function_name;	// UTF-8
		const char* extra_args;		// UTF-8。引数を ; で区切ったもの
	};

	// エディタの EngineAPIStructs.ShaderGroupData と同じ並び
	struct shader_group_data
	{
		u32 type;
		u32 count;
		u32 data_size;
		u8* data;
	};

	utl::vector<std::wstring> split_args(const char* args)
	{
		utl::vector<std::wstring> result{};
		if (!args) return result;

		std::string_view rest{ args };
		while (!rest.empty())
		{
			const size_t end{ rest.find(';') };
			const std::string arg{ rest.substr(0, end) };
			if (!arg.empty()) result.emplace_back(to_wstring(arg.c_str()));
			if (end == std::string_view::npos) break;
			rest.remove_prefix(end + 1);
		}

		return result;
	}
} // 匿名名前空間

// コンパイル済みのシェーダーを、キーごとに1つのシェーダーグループとしてエンジンに登録する(Primal Day 076)。
EDITOR_INTERFACE id::id_type AddShaderGroup(shader_group_data* data)
{
	if (!data || data->type >= graphics::shader_type::count || !data->count || !data->data_size || !data->data) return id::invalid_id;
	const u32 count{ data->count };

	// data->data =
	// {
	//    u32 keys[count];
	//    struct {
	//      u64 bytecode_length;
	//      u8  hash[hash_length];
	//      u8  bytecode[bytecode_length];
	//    } blocks[count];
	// }
	const u8* const end{ data->data + data->data_size };
	if ((u64)count * sizeof(u32) > data->data_size) return id::invalid_id;
	utl::blob_stream_reader blob{ data->data };
	const u32* const keys{ (const u32*)blob.position() };
	blob.skip(count * sizeof(u32));

	utl::vector<const u8*> shader_pointers(count);
	for (u32 i{ 0 }; i < count; ++i)
	{
		if (blob.position() + sizeof(u64) > end) return id::invalid_id;
		const u64 byte_code_length{ *(const u64*)blob.position() };
		const u64 block_size{ sizeof(u64) + content::compiled_shader::hash_length + byte_code_length };
		if (!byte_code_length || block_size > (u64)(end - blob.position())) return id::invalid_id;
		shader_pointers[i] = blob.position();
		blob.skip(block_size);
	}

	if (blob.position() != end) return id::invalid_id;

	return content::add_shader_group(shader_pointers.data(), count, keys);
}

EDITOR_INTERFACE void RemoveShaderGroup(id::id_type id)
{
	if (!id::is_valid(id)) return;
	content::remove_shader_group(id);
}

// エディタから渡されたシェーダーのソースをコンパイルする(Primal Day 076)。
// コンパイルできたら TRUE。失敗しても、DXCのエラーがあれば byte_code_size = 0 でエラーを返す。
// 返すバッファ(byte_code_error_assembly_hash)は CoTaskMemAlloc で確保するので、呼び出し側が CoTaskMemFree で解放する。
// 並び: バイトコード, エラー, 逆アセンブル, ハッシュ
// NOTE: エディタはエンジンのシェーダーのフォルダを extra_args の -I で渡す(ShaderGroup.ExtraArgs としてアセットに保存される)。
//       -I があるときは、作業フォルダからの相対パスの既定のフォルダを -I に付けない(get_args()。#150)。
EDITOR_INTERFACE u32 CompileShader(shader_data* data)
{
	if (!data) return FALSE;
	data->byte_code_size = data->errors_size = data->assembly_size = data->hash_size = 0;
	data->byte_code_error_assembly_hash = nullptr;
	if (!data->code || !data->code_size || !data->function_name || !*data->function_name || data->type >= graphics::shader_type::count) return FALSE;

	shader_file_info info{};
	info.function = data->function_name;
	info.type = (graphics::shader_type::type)data->type;

	utl::vector<std::wstring> extra_args{ split_args(data->extra_args) };

	std::unique_ptr<u8[]> compiled_shader{ compile_shader(info, data->code, data->code_size, extra_args, true) };
	if (!compiled_shader) return FALSE;

	u64 byte_code_size{ 0 };
	u64 errors_size{ 0 };
	u64 assembly_size{ 0 };
	constexpr u64 hash_size{ content::compiled_shader::hash_length };
	{
		utl::blob_stream_reader blob{ compiled_shader.get() };
		byte_code_size = blob.read<u64>();
		blob.skip(hash_size + byte_code_size);
		errors_size = blob.read<u64>();
		assembly_size = blob.read<u64>();
	}

	const u64 buffer_size{ byte_code_size + errors_size + assembly_size + hash_size };
	if (buffer_size > UINT32_MAX) return FALSE;
	u8* const buffer{ (u8*)CoTaskMemAlloc(buffer_size) };
	if (!buffer) return FALSE;

	{
		utl::blob_stream_reader blob{ compiled_shader.get() };
		blob.skip(sizeof(u64)); // バイトコードのサイズを飛ばす
		blob.read(&buffer[buffer_size - hash_size], hash_size);
		blob.read(buffer, byte_code_size);
		blob.skip(2 * sizeof(u64)); // エラーと逆アセンブルのサイズを飛ばす
		blob.read(&buffer[byte_code_size], errors_size + assembly_size);
	}

	data->byte_code_error_assembly_hash = buffer;
	data->byte_code_size = (u32)byte_code_size;
	data->errors_size = (u32)errors_size;
	data->assembly_size = (u32)assembly_size;
	data->hash_size = byte_code_size ? (u32)hash_size : 0;

	return byte_code_size ? TRUE : FALSE;
}

// ビューポートに描くジオメトリ(Geometry コンポーネントの ID)を設定する
EDITOR_INTERFACE void SetGeometryIds(u32 surface_id, id::id_type* geometry_ids, u32 count)
{
	std::lock_guard lock{ mutex };
	assert(surface_id < surfaces.size());

	viewport_surface& surface{ surfaces[surface_id] };
	surface.geometry_ids.resize(count);

	if (count)
	{
		memcpy(surface.geometry_ids.data(), geometry_ids, count * sizeof(id::id_type));
	}
}

// ビューポートを 1 フレーム描く。camera_id が無効ならビューポートのカメラを使う
// NOTE: サーフェスがないとき(エンジンの初期化の失敗、大きさが 0)と、ライトカリングのタイルより小さいときは何もしない
//       (delight::cull_lights() の assert)。
EDITOR_INTERFACE void RenderFrame(u32 surface_id, id::id_type camera_id, u64 light_set)
{
	std::lock_guard lock{ mutex };
	assert(surface_id < surfaces.size());

	viewport_surface& surface{ surfaces[surface_id] };
	if (!surface.surface.is_valid() ||
		surface.surface.width() < graphics::min_offscreen_surface_size || surface.surface.height() < graphics::min_offscreen_surface_size) return;

	// TEMPORARY
	light_set = 0;

	const u32 count{ (u32)surface.geometry_ids.size() };
	const u64 item_id_buffer_size{ sizeof(id::id_type) * count };
	const u64 threshold_buffer_size{ sizeof(f32) * count };
	const u64 entity_id_buffer_size{ sizeof(game_entity::entity_id) * count };

	frame_info_buffer.resize(item_id_buffer_size + threshold_buffer_size + entity_id_buffer_size);

	id::id_type* const item_ids{ (id::id_type*)frame_info_buffer.data() };
	f32* const thresholds{ (f32*)(frame_info_buffer.data() + item_id_buffer_size) };
	game_entity::entity_id* const entity_ids{ (game_entity::entity_id*)(frame_info_buffer.data() + item_id_buffer_size + threshold_buffer_size) };

	// NOTE: LOD の距離は、描画に使うカメラから測る(Primal は常にビューポートのカメラから測る)
	const graphics::camera view_camera{ !id::is_valid(camera_id) ? surface.camera : graphics::camera{ graphics::camera_id{ camera_id } } };

	if (count)
	{
		const id::id_type* const geometry_ids{ surface.geometry_ids.data() };
		geometry::get_entity_ids(geometry_ids, entity_ids, count);
		geometry::get_render_item_ids(geometry_ids, item_ids, count);
		calculate_thresholds(entity_ids, thresholds, count, view_camera);
	}

	graphics::frame_info info{};
	info.render_item_ids = item_ids;
	info.render_item_count = count;
	info.thresholds = thresholds;
	info.camera_id = view_camera.get_id();
	info.light_set_key = light_set;

	surface.surface.render(info);
	++surface.drawn_frame_count;
}

// RenderFrame がこのビューポートを実際に描いた回数の累計(#209)。何もしなかったフレーム(サーフェスがない、小さい)は数えない。
// NOTE: エディタは render thread のコールバックから呼ぶ。UI スレッドから呼ぶと、描画の間ミューテックスを待つ(#213)。
EDITOR_INTERFACE u64 GetDrawnFrameCount(u32 surface_id)
{
	std::lock_guard lock{ mutex };
	return surface_id < surfaces.size() ? surfaces[surface_id].drawn_frame_count : 0;
}

// エディタのカメラ(EditorCamera)の位置と回転を、ビューポートのカメラに設定する(Primal Day 085)
// rot はラジアンのピッチ、ヨー、ロール(エディタのカメラの内部の値。エンティティの Transform の回転の単位とは別)。
// NOTE: Primal はカメラがあることを assert で確かめる。DXForgeEX は大きさが 0 の間カメラを作らないので、値を覚えておき、
//       カメラを作ったときに反映する(apply_camera_settings)。範囲外と消したあとのビューポートでは何もしない。
EDITOR_INTERFACE void UpdateEditorCamera(u32 surface_id, f32 pos_x, f32 pos_y, f32 pos_z, f32 rot_x, f32 rot_y, f32 rot_z)
{
	std::lock_guard lock{ mutex };
	if (!is_editor_camera_target(surface_id)) return;
	viewport_surface& surface{ surfaces[surface_id] };
	surface.camera_settings.position = { pos_x, pos_y, pos_z };
	surface.camera_settings.rotation = to_quat({ rot_x, rot_y, rot_z }, false);
	surface.camera_settings.has_transform = true;
	apply_camera_settings(surface);
}

// ビューポートのカメラの近クリップ面と遠クリップ面を設定する(Primal Day 085)。カメラがなければ、作ったときに反映する。
EDITOR_INTERFACE void SetCameraRange(u32 surface_id, f32 near_z, f32 far_z)
{
	std::lock_guard lock{ mutex };
	assert(near_z > 0 && far_z >= near_z);
	if (!is_editor_camera_target(surface_id)) return;
	viewport_surface& surface{ surfaces[surface_id] };
	surface.camera_settings.near_z = near_z;
	surface.camera_settings.far_z = far_z;
	surface.camera_settings.has_range = true;
	apply_camera_settings(surface);
}

// ビューポートのカメラの視野角(度数)を設定する(Primal Day 085)。カメラがなければ、作ったときに反映する。
// NOTE: エンジンのカメラは field_of_view * pi を視野角にするので、180 で割る。
EDITOR_INTERFACE void SetCameraFoV(u32 surface_id, f32 fov)
{
	std::lock_guard lock{ mutex };
	assert(fov > 0);
	if (!is_editor_camera_target(surface_id)) return;
	viewport_surface& surface{ surfaces[surface_id] };
	surface.camera_settings.field_of_view = fov * (1.f / 180.f);
	surface.camera_settings.has_field_of_view = true;
	apply_camera_settings(surface);
}
