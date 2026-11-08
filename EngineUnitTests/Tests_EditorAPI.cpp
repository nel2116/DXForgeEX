// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_EditorAPI.cpp]
// 作成日 : 2026/11/06
// 作成者 : 田中ミノル
// 概要
// 　エディタが呼ぶ DLL の関数(EntityAPI.cpp、EngineAPI.cpp)のうち、GPU のいらない経路のテスト (Primal Day 082, #201)
// 更新履歴
// 2026/11/06 新規作成
// 2026/11/08 エンティティの回転をクォータニオンで渡すテストにした (#205)
// 2026/11/08 作成後のエンティティの位置・回転・拡大率を読み書きする Get/Set のテストを追加 (Primal Day 086, #244)
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

// ====== 作成後のトランスフォームの読み書き (Primal Day 086, #244) ======

namespace
{
	// DirectX の q と -q は同じ回転なので、符号をそろえて比べる
	bool same_rotation(const math::v4& a, const math::v4& b, f32 tolerance = 1e-5f)
	{
		const f32 sign{ (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) < 0.f ? -1.f : 1.f };
		return std::fabs(a.x - sign * b.x) < tolerance && std::fabs(a.y - sign * b.y) < tolerance &&
			std::fabs(a.z - sign * b.z) < tolerance && std::fabs(a.w - sign * b.w) < tolerance;
	}

	bool same_position(const math::v3& a, const math::v3& b, f32 tolerance = 1e-5f)
	{
		return std::fabs(a.x - b.x) < tolerance && std::fabs(a.y - b.y) < tolerance && std::fabs(a.z - b.z) < tolerance;
	}

	math::v4 quat_from_axis_angle(f32 x, f32 y, f32 z, f32 radians)
	{
		using namespace DirectX;
		math::v4 q{};
		XMStoreFloat4(&q, XMQuaternionRotationAxis(XMVectorSet(x, y, z, 0.f), radians));
		return q;
	}

	math::v4 quat_multiply(const math::v4& first, const math::v4& then)
	{
		using namespace DirectX;
		math::v4 q{};
		XMStoreFloat4(&q, XMQuaternionMultiply(XMLoadFloat4(&first), XMLoadFloat4(&then)));
		return q;
	}

	id::id_type create_entity_with(const math::v3& position, const math::v4& rotation, const math::v3& scale)
	{
		test_descriptor desc{};
		desc.position[0] = position.x; desc.position[1] = position.y; desc.position[2] = position.z;
		desc.rotation[0] = rotation.x; desc.rotation[1] = rotation.y; desc.rotation[2] = rotation.z; desc.rotation[3] = rotation.w;
		desc.scale[0] = scale.x; desc.scale[1] = scale.y; desc.scale[2] = scale.z;
		return CreateGameEntity(&desc);
	}

	// 3 つのエンティティを作り、真ん中を削除して死んだ ID にする
	struct three_entities
	{
		id::id_type ids[3]{};

		three_entities(const math::v4& rotation)
		{
			for (u32 i{ 0 }; i < 3; ++i)
			{
				ids[i] = create_entity_with({ (f32)i, 0.f, 0.f }, rotation, { 1.f, 1.f, 1.f });
			}
			RemoveGameEntity(ids[1]);
		}
		~three_entities()
		{
			RemoveGameEntity(ids[0]);
			RemoveGameEntity(ids[2]);
		}
	};

	constexpr u32 frame_absolute{ 0 };	// transform::space::absolute
	constexpr u32 frame_local{ 1 };		// transform::space::local
	constexpr u32 frame_world{ 2 };		// transform::space::world
}

// DXForgeEngineDLL/EntityAPI.cpp (#244)
extern "C" u32 GetPosition(const id::id_type* ids, f32* x, f32* y, f32* z, u32 count);
extern "C" u32 GetRotation(const id::id_type* ids, f32* x, f32* y, f32* z, f32* w, u32 count);
extern "C" u32 GetScale(const id::id_type* ids, f32* x, f32* y, f32* z, u32 count);
extern "C" u32 SetPosition(const id::id_type* ids, const f32* x, const f32* y, const f32* z, u32 count, u32 frame);
extern "C" u32 SetRotation(const id::id_type* ids, const f32* x, const f32* y, const f32* z, const f32* w, u32 count, u32 frame);
extern "C" u32 SetScale(const id::id_type* ids, const f32* x, const f32* y, const f32* z, u32 count);

DXFORGE_TEST_CASE(editor_api_frame_values_match_transform_space)
{
	CHECK(frame_absolute == transform::space::absolute);
	CHECK(frame_local == transform::space::local);
	CHECK(frame_world == transform::space::world);
}

// 絶対値の Set と Get の往復。死んだ ID は飛ばして 0 を返し、ほかの ID は書き換え、Get は死んだ ID の出力を書かない
DXFORGE_TEST_CASE(editor_api_set_and_get_absolute_transform_skip_a_dead_id)
{
	const three_entities e{ { 0.f, 0.f, 0.f, 1.f } };

	const f32 px[]{ 1.f, 2.f, 3.f }, py[]{ -4.f, -5.f, -6.f }, pz[]{ 7.f, 8.f, 9.f };
	CHECK(SetPosition(e.ids, px, py, pz, 3, frame_absolute) == 0);
	const f32 sx[]{ 2.f, 3.f, 4.f }, sy[]{ 1.f, 1.f, 1.f }, sz[]{ 0.5f, 0.5f, 0.5f };
	CHECK(SetScale(e.ids, sx, sy, sz, 3) == 0);
	// 長さが 1 でないクォータニオンは正規化して保存する
	const math::v4 q0{ quat_from_axis_angle(0.f, 1.f, 0.f, 0.7f) };
	const math::v4 q2{ 0.2f, -0.4f, 0.6f, 0.66332496f };
	const f32 rx[]{ q0.x, 9.f, q2.x * 2.f }, ry[]{ q0.y, 9.f, q2.y * 2.f }, rz[]{ q0.z, 9.f, q2.z * 2.f }, rw[]{ q0.w, 9.f, q2.w * 2.f };
	CHECK(SetRotation(e.ids, rx, ry, rz, rw, 3, frame_absolute) == 0);

	constexpr f32 untouched{ -123.f };
	f32 gx[3]{ untouched, untouched, untouched }, gy[3]{ untouched, untouched, untouched }, gz[3]{ untouched, untouched, untouched }, gw[3]{ untouched, untouched, untouched };
	CHECK(GetPosition(e.ids, gx, gy, gz, 3) == 0);
	CHECK(same_position({ gx[0], gy[0], gz[0] }, { 1.f, -4.f, 7.f }));
	CHECK(same_position({ gx[2], gy[2], gz[2] }, { 3.f, -6.f, 9.f }));
	CHECK(gx[1] == untouched && gy[1] == untouched && gz[1] == untouched);

	CHECK(GetScale(e.ids, gx, gy, gz, 3) == 0);
	CHECK(same_position({ gx[0], gy[0], gz[0] }, { 2.f, 1.f, 0.5f }));
	CHECK(same_position({ gx[2], gy[2], gz[2] }, { 4.f, 1.f, 0.5f }));

	CHECK(GetRotation(e.ids, gx, gy, gz, gw, 3) == 0);
	CHECK(same_rotation({ gx[0], gy[0], gz[0], gw[0] }, q0));
	CHECK(same_rotation({ gx[2], gy[2], gz[2], gw[2] }, q2));
	CHECK(gw[1] == untouched);

	// 生きている ID だけなら 1。無効な ID でも落ちない。count が 0 なら何もしない
	const id::id_type alive[]{ e.ids[0], e.ids[2] };
	CHECK(GetPosition(alive, gx, gy, gz, 2) == 1);
	const id::id_type invalid[]{ id::invalid_id };
	CHECK(GetPosition(invalid, gx, gy, gz, 1) == 0);
	CHECK(SetPosition(invalid, px, py, pz, 1, frame_absolute) == 0);
	CHECK(SetPosition(nullptr, nullptr, nullptr, nullptr, 0, frame_absolute) == 1);
	CHECK(GetPosition(alive, gx, gy, gz, 2) == 1);
	CHECK(same_position({ gx[0], gy[0], gz[0] }, { 1.f, -4.f, 7.f }));
}

// すでに回転しているエンティティでは、ローカルとワールドの差分の回転は結果が違う
DXFORGE_TEST_CASE(editor_api_set_rotation_local_and_world_deltas_differ)
{
	// Y 軸まわりに 90 度回したエンティティを、X 軸まわりに 90 度回す
	const math::v4 initial{ quat_from_axis_angle(0.f, 1.f, 0.f, math::half_pi) };
	const math::v4 delta{ quat_from_axis_angle(1.f, 0.f, 0.f, math::half_pi) };
	const f32 dx[]{ delta.x, delta.x, delta.x }, dy[]{ delta.y, delta.y, delta.y }, dz[]{ delta.z, delta.z, delta.z }, dw[]{ delta.w, delta.w, delta.w };
	f32 gx[3]{}, gy[3]{}, gz[3]{}, gw[3]{};

	math::v4 local_result{};
	{
		const three_entities e{ initial };
		CHECK(SetRotation(e.ids, dx, dy, dz, dw, 3, frame_local) == 0);
		CHECK(GetRotation(e.ids, gx, gy, gz, gw, 3) == 0);
		local_result = { gx[0], gy[0], gz[0], gw[0] };
		CHECK(same_rotation(local_result, quat_multiply(delta, initial)));
		CHECK(same_rotation({ gx[2], gy[2], gz[2], gw[2] }, local_result));
		// ローカルの X 軸(right)まわりなので、right は変わらず (0, 0, -1) のまま
		const math::v3 right{ game_entity::entity{ game_entity::entity_id{ e.ids[0] } }.right() };
		CHECK(same_position(right, { 0.f, 0.f, -1.f }));
	}

	math::v4 world_result{};
	{
		const three_entities e{ initial };
		CHECK(SetRotation(e.ids, dx, dy, dz, dw, 3, frame_world) == 0);
		CHECK(GetRotation(e.ids, gx, gy, gz, gw, 3) == 0);
		world_result = { gx[0], gy[0], gz[0], gw[0] };
		CHECK(same_rotation(world_result, quat_multiply(initial, delta)));
		CHECK(same_rotation({ gx[2], gy[2], gz[2], gw[2] }, world_result));
		// ワールドの X 軸まわりなので、front(Y 軸まわり 90 度で +X)は X 軸上にあり変わらない
		const math::v3 front{ game_entity::entity{ game_entity::entity_id{ e.ids[0] } }.front() };
		CHECK(same_position(front, { 1.f, 0.f, 0.f }));
	}

	CHECK(!same_rotation(local_result, world_result, 1e-3f));
}

// すでに回転しているエンティティでは、ローカルとワールドの差分の移動は結果が違う
DXFORGE_TEST_CASE(editor_api_set_position_local_and_world_deltas_differ)
{
	// Y 軸まわりに 90 度: right = (0, 0, -1)、up = (0, 1, 0)、front = (1, 0, 0)
	const math::v4 initial{ quat_from_axis_angle(0.f, 1.f, 0.f, math::half_pi) };
	const f32 dx[]{ 1.f, 1.f, 1.f }, dy[]{ 2.f, 2.f, 2.f }, dz[]{ 3.f, 3.f, 3.f };
	f32 gx[3]{}, gy[3]{}, gz[3]{};
	{
		const three_entities e{ initial };
		CHECK(SetPosition(e.ids, dx, dy, dz, 3, frame_local) == 0);
		CHECK(GetPosition(e.ids, gx, gy, gz, 3) == 0);
		// 1 * right + 2 * up + 3 * front を足す
		CHECK(same_position({ gx[0], gy[0], gz[0] }, { 0.f + 3.f, 2.f, -1.f }));
		CHECK(same_position({ gx[2], gy[2], gz[2] }, { 2.f + 3.f, 2.f, -1.f }));
	}
	{
		const three_entities e{ initial };
		CHECK(SetPosition(e.ids, dx, dy, dz, 3, frame_world) == 0);
		CHECK(GetPosition(e.ids, gx, gy, gz, 3) == 0);
		CHECK(same_position({ gx[0], gy[0], gz[0] }, { 1.f, 2.f, 3.f }));
		CHECK(same_position({ gx[2], gy[2], gz[2] }, { 3.f, 2.f, 3.f }));
	}
}

// Set のあと、描画に使うワールド行列が新しい値で作り直される
DXFORGE_TEST_CASE(editor_api_set_transform_updates_the_world_matrix)
{
	using namespace DirectX;
	const id::id_type id{ create_entity_with({ 0.f, 0.f, 0.f }, { 0.f, 0.f, 0.f, 1.f }, { 1.f, 1.f, 1.f }) };
	REQUIRE(id::is_valid(id));
	const game_entity::entity_id entity_id{ id };

	// 作成直後の行列を一度作らせておき、Set で作り直されることを確かめる
	math::m4x4 world{}, inverse_world{};
	transform::get_transform_matrices(entity_id, world, inverse_world);

	const f32 px[]{ 5.f }, py[]{ -2.f }, pz[]{ 3.f };
	const f32 sx[]{ 2.f }, sy[]{ 3.f }, sz[]{ 4.f };
	const math::v4 q{ quat_from_axis_angle(0.f, 0.f, 1.f, 0.5f) };
	const f32 rx[]{ q.x }, ry[]{ q.y }, rz[]{ q.z }, rw[]{ q.w };
	const id::id_type ids[]{ id };
	CHECK(SetPosition(ids, px, py, pz, 1, frame_absolute) == 1);
	CHECK(SetScale(ids, sx, sy, sz, 1) == 1);
	CHECK(SetRotation(ids, rx, ry, rz, rw, 1, frame_absolute) == 1);

	transform::get_transform_matrices(entity_id, world, inverse_world);
	math::m4x4 expected{};
	XMStoreFloat4x4(&expected, XMMatrixAffineTransformation(XMVectorSet(2.f, 3.f, 4.f, 0.f), XMQuaternionIdentity(), XMLoadFloat4(&q), XMVectorSet(5.f, -2.f, 3.f, 1.f)));
	for (u32 r{ 0 }; r < 4; ++r)
	{
		for (u32 c{ 0 }; c < 4; ++c)
		{
			CHECK(std::fabs(world.m[r][c] - expected.m[r][c]) < 1e-5f);
		}
	}

	// 原点は新しい位置に描かれる
	CHECK(same_position({ world._41, world._42, world._43 }, { 5.f, -2.f, 3.f }));
	RemoveGameEntity(id);
}
