// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_EditorAPI.cpp]
// 作成日 : 2026/11/06
// 作成者 : 田中ミノル
// 概要
// 　エディタが呼ぶ DLL の関数(EntityAPI.cpp、EngineAPI.cpp)のうち、GPU のいらない経路のテスト (Primal Day 082, #201)
// 更新履歴
// 2026/11/06 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"
#include "CommonHeaders.h"
#include "Components/Entity.h"
#include "Components/Transform.h"
#include "Components/Script.h"
#include "../DXForgeEngineDLL/EngineInit.h"

#include <cmath>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

using namespace dxforge;

namespace
{
	// NOTE: DXForgeEngineDLL/EntityAPI.cpp の game_entity_descriptor と同じ並び
	struct test_descriptor
	{
		f32 position[3]{};
		f32 rotation[3]{};
		f32 scale[3]{ 1.f, 1.f, 1.f };
		script::detail::script_creator script_creator{ nullptr };
		id::id_type geometry_content_id{ id::invalid_id };
		u32 material_count{ 0 };
		id::id_type* material_ids{ nullptr };
	};

	bool nearly_equal(f32 a, f32 b)
	{
		return std::fabs(a - b) < 1e-5f;
	}

	class editor_api_test_script : public script::entity_script
	{
	public:
		explicit editor_api_test_script(game_entity::entity entity) : script::entity_script{ entity } {}
	};
}

REGISTER_SCRIPT(editor_api_test_script);

// DXForgeEngineDLL/EntityAPI.cpp
extern "C" id::id_type CreateGameEntity(test_descriptor* e);
extern "C" void RemoveGameEntity(id::id_type id);
extern "C" u32 UpdateComponent(id::id_type entity_id, test_descriptor* e, component_type::type type);
extern "C" id::id_type GetComponentId(id::id_type entity_id, component_type::type type);
// DXForgeEngineDLL/EngineAPI.cpp
extern "C" void ShutdownEngine();
extern "C" u32 CreateRenderSurface(HWND host, s32 width, s32 height);
extern "C" void RemoveRenderSurface(u32 id);
extern "C" intptr_t GetWindowHandle(u32 id);
extern "C" void ResizeRenderSurface(u32 id);
extern "C" void RenderFrame(u32 surface_id, id::id_type camera_id, u64 light_set);

DXFORGE_TEST_CASE(math_to_radians_and_to_degrees_convert_each_axis)
{
	const math::v3 radians{ math::to_radians({ 180.f, 90.f, -45.f }) };
	CHECK(nearly_equal(radians.x, math::pi));
	CHECK(nearly_equal(radians.y, math::half_pi));
	CHECK(nearly_equal(radians.z, -math::pi * 0.25f));

	const math::v3 degrees{ math::to_degrees({ math::pi, math::half_pi, 0.f }) };
	CHECK(std::fabs(degrees.x - 180.f) < 1e-3f);
	CHECK(std::fabs(degrees.y - 90.f) < 1e-3f);
	CHECK(degrees.z == 0.f);
}

// エディタの Transform の回転は度数法(修正前はラジアンとして解釈していた)
DXFORGE_TEST_CASE(editor_api_create_game_entity_reads_rotation_in_degrees)
{
	test_descriptor desc{};
	desc.rotation[1] = 90.f;
	const id::id_type id{ CreateGameEntity(&desc) };
	REQUIRE(id::is_valid(id));

	const game_entity::entity entity{ game_entity::entity_id{ id } };
	const math::v4 rotation{ entity.rotation() };
	constexpr f32 half_sqrt2{ 0.70710678f };
	CHECK(nearly_equal(rotation.x, 0.f));
	CHECK(nearly_equal(rotation.y, half_sqrt2));
	CHECK(nearly_equal(rotation.z, 0.f));
	CHECK(nearly_equal(rotation.w, half_sqrt2));

	RemoveGameEntity(id);
}

DXFORGE_TEST_CASE(editor_api_update_component_replaces_and_removes_a_script)
{
	test_descriptor desc{};
	const id::id_type id{ CreateGameEntity(&desc) };
	REQUIRE(id::is_valid(id));

	// Transform の ID はエンティティの ID と同じ。Script と Geometry はまだない
	CHECK(GetComponentId(id, component_type::transform) == id);
	CHECK(!id::is_valid(GetComponentId(id, component_type::script)));
	CHECK(!id::is_valid(GetComponentId(id, component_type::geometry)));

	desc.script_creator = script::detail::get_script_creator(script::detail::string_hash()("editor_api_test_script"));
	REQUIRE(desc.script_creator != nullptr);
	CHECK(UpdateComponent(id, &desc, component_type::script) == 1);
	const id::id_type first_script{ GetComponentId(id, component_type::script) };
	CHECK(id::is_valid(first_script));

	// もう一度呼ぶと作り直す
	CHECK(UpdateComponent(id, &desc, component_type::script) == 1);
	CHECK(id::is_valid(GetComponentId(id, component_type::script)));

	// 作るものがなければ、削除だけを行って 0 を返す
	desc.script_creator = nullptr;
	CHECK(UpdateComponent(id, &desc, component_type::script) == 0);
	CHECK(!id::is_valid(GetComponentId(id, component_type::script)));

	RemoveGameEntity(id);
}

// エンジンの初期化に失敗したあとでも、エディタはビューポートを作る。ウィンドウだけを作り、D3D12 を触らない
DXFORGE_TEST_CASE(editor_api_viewport_without_an_initialized_engine_has_only_a_window)
{
	REQUIRE(!editor::is_engine_initialized());
	const HWND host{ CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 64, 64, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr) };
	REQUIRE(host != nullptr);

	const u32 id{ CreateRenderSurface(host, 64, 64) };
	const HWND window{ (HWND)GetWindowHandle(id) };
	CHECK(window != nullptr);
	CHECK(IsWindow(window) != FALSE);

	SetWindowPos(window, nullptr, 0, 0, 32, 32, SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);
	ResizeRenderSurface(id);
	RenderFrame(id, id::invalid_id, 0);
	ShutdownEngine();

	RemoveRenderSurface(id);
	CHECK(IsWindow(window) == FALSE);
	// エディタのリサイズのタイマーは、消したあとにも動くことがある
	ResizeRenderSurface(id);

	DestroyWindow(host);
}
