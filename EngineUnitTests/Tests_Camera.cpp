// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_Camera.cpp]
// 作成日 : 2026/10/18
// 作成者 : 田中ミノル
// 概要 :
// Graphics/Direct3D12/D3D12Camera.cppの上方向の扱いのテスト(GPUデバイス不要)
// 更新履歴
// 2026/10/18 新規作成 (#62)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"
#include "CommonHeaders.h"
#include "Components/Entity.h"
#include "Components/Transform.h"
#include "Graphics/Direct3D12/D3D12Camera.h"

#include <cmath>
#include <type_traits>

using namespace dxforge;

// NOTE: d3d12_cameraはエンティティの位置と回転からビュー行列を計算するだけで、
//		GPUデバイスには触れない。そのためクラスを直接生成すればヘッドレスで検証できる。
//		エンティティの状態はプロセス全体で共有されるので、生成したものは必ず削除する。

namespace
{
	// 上方向を明示的に設定するsetterがあるかを検出する(C++17のため検出イディオムを使う)
	template<typename T, typename = void>
	struct has_up_setter : std::false_type {};
	template<typename T>
	struct has_up_setter<T, std::void_t<decltype(std::declval<const T&>().up(std::declval<math::v3>()))>> : std::true_type {};

	// 初期化情報に上方向のメンバがあるかを検出する
	template<typename T, typename = void>
	struct has_up_member : std::false_type {};
	template<typename T>
	struct has_up_member<T, std::void_t<decltype(std::declval<T&>().up)>> : std::true_type {};

	bool nearly_equal(f32 a, f32 b)
	{
		return std::fabs(a - b) < 1e-5f;
	}

	bool nearly_equal(DirectX::XMVECTOR v, math::v3 expected)
	{
		math::v3 actual;
		DirectX::XMStoreFloat3(&actual, v);
		return nearly_equal(actual.x, expected.x) && nearly_equal(actual.y, expected.y) && nearly_equal(actual.z, expected.z);
	}

	// Z軸まわりに90度回転したエンティティを作る(ローカルの上方向はワールドの-X)
	game_entity::entity create_rolled_entity()
	{
		transform::init_info transform_info{};
		const f32 half{ DirectX::XM_PIDIV4 };
		transform_info.rotation[2] = std::sin(half);
		transform_info.rotation[3] = std::cos(half);
		game_entity::entity_info entity_info{};
		entity_info.transform = &transform_info;
		return game_entity::create(entity_info);
	}
}

// 修正前は上方向のsetterと初期化情報のupがあり、指定した値はupdate()で黙って捨てられていた。
// 上方向はエンティティの回転だけで決めるため、値を受け取るAPI自体が存在しないことを確かめる。
DXFORGE_TEST_CASE(camera_has_no_api_that_accepts_an_up_vector)
{
	CHECK(!has_up_setter<graphics::camera>::value);
	CHECK(!has_up_setter<graphics::d3d12::camera::d3d12_camera>::value);
	CHECK(!has_up_member<graphics::camera_init_info>::value);
	CHECK(!has_up_member<graphics::perspective_camera_init_info>::value);
	CHECK(!has_up_member<graphics::orthographic_camera_init_info>::value);
}

DXFORGE_TEST_CASE(camera_up_follows_entity_rotation)
{
	const game_entity::entity entity{ create_rolled_entity() };
	REQUIRE(entity.is_valid());
	const math::v3 entity_up{ entity.up() };
	REQUIRE(nearly_equal(entity_up.x, -1.f));
	REQUIRE(nearly_equal(entity_up.y, 0.f));

	graphics::d3d12::camera::d3d12_camera camera{ graphics::perspective_camera_init_info{ entity.get_id() } };
	CHECK(nearly_equal(camera.up(), entity_up));
	camera.update();
	CHECK(nearly_equal(camera.up(), entity_up));

	game_entity::remove(entity.get_id());
}
