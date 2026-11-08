// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_EditorAPI.cpp]
// 作成日 : 2026/11/06
// 作成者 : 田中ミノル
// 概要
// 　エディタが呼ぶ DLL の関数(EntityAPI.cpp、EngineAPI.cpp)のうち、GPU のいらない経路のテスト (Primal Day 082, #201)
// 更新履歴
// 2026/11/06 新規作成
// 2026/11/08 エンティティの回転をクォータニオンで渡すテストにした (#205)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"
#include "CommonHeaders.h"
#include "Components/Entity.h"
#include "Components/Transform.h"
#include "Components/Script.h"
#include "../DXForgeEngineDLL/EngineInit.h"

#include <cmath>
#include <cstring>

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
		f32 rotation[4]{ 0.f, 0.f, 0.f, 1.f };	// クォータニオン(#205)
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
// エディタは回転をクォータニオンで渡し、DLL は変換せずにそのまま使う(#205)
DXFORGE_TEST_CASE(editor_api_create_game_entity_takes_rotation_as_quaternion)
{
	// Y 軸まわりに 90 度と、どの成分も 0 でない回転
	constexpr f32 half_sqrt2{ 0.70710678f };
	const f32 cases[][4]{ { 0.f, half_sqrt2, 0.f, half_sqrt2 }, { 0.1f, -0.3f, 0.5f, 0.80622577f } };
	for (const auto& q : cases)
	{
		test_descriptor desc{};
		memcpy(&desc.rotation[0], &q[0], sizeof(desc.rotation));
		const id::id_type id{ CreateGameEntity(&desc) };
		REQUIRE(id::is_valid(id));

		const game_entity::entity entity{ game_entity::entity_id{ id } };
		const math::v4 rotation{ entity.rotation() };
		CHECK(nearly_equal(rotation.x, q[0]));
		CHECK(nearly_equal(rotation.y, q[1]));
		CHECK(nearly_equal(rotation.z, q[2]));
		CHECK(nearly_equal(rotation.w, q[3]));
		RemoveGameEntity(id);
	}
}

// エディタ(DXForgeEditor/Utilities/EulerRotation.cs)と同じオイラー角の順序: x = ピッチ、y = ヨー、z = ロールで Z -> X -> Y。
// 期待値は C# の Quaternion.CreateFromYawPitchRoll で求めたもの。DXForgeCli/Tests/ops_cli_test.ps1 も同じ表で C# の側を確かめる
DXFORGE_TEST_CASE(euler_order_matches_the_editor)
{
	using namespace DirectX;
	struct euler_case { f32 degrees[3]; f32 quaternion[4]; };
	const euler_case cases[]
	{
		{ { 0.f, 90.f, 0.f }, { 0.f, 0.70710678f, 0.f, 0.70710678f } },
		{ { 90.f, 0.f, 0.f }, { 0.70710678f, 0.f, 0.f, 0.70710678f } },
		{ { 0.f, 0.f, 90.f }, { 0.f, 0.f, 0.70710678f, 0.70710678f } },
		{ { 30.f, 45.f, 60.f }, { 0.39190382f, 0.20056215f, 0.36042339f, 0.82236314f } },
		{ { -120.f, 200.f, 15.f }, { 0.21336880f, 0.46856225f, 0.83423936f, -0.19740298f } },
	};
	for (const auto& c : cases)
	{
		const math::v3 radians{ math::to_radians({ c.degrees[0], c.degrees[1], c.degrees[2] }) };
		math::v4 q{};
		XMStoreFloat4(&q, XMQuaternionRotationRollPitchYawFromVector(XMLoadFloat3(&radians)));
		// q と -q は同じ回転
		const f32 sign{ (q.x * c.quaternion[0] + q.y * c.quaternion[1] + q.z * c.quaternion[2] + q.w * c.quaternion[3]) < 0.f ? -1.f : 1.f };
		CHECK(nearly_equal(sign * q.x, c.quaternion[0]));
		CHECK(nearly_equal(sign * q.y, c.quaternion[1]));
		CHECK(nearly_equal(sign * q.z, c.quaternion[2]));
		CHECK(nearly_equal(sign * q.w, c.quaternion[3]));
	}
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
