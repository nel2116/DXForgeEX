// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Light.cpp]
// 作成日 : 2025/01/18
// 作成者 : 田中ミノル
// 概要 :
// 更新履歴
// 2025/01/18 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "EngineAPI/GameEntity.h"
#include "EngineAPI/Light.h"
#include "EngineAPI/TransformComponent.h"
#include "Graphics/Renderer.h"

using namespace dxforge;

game_entity::entity create_one_game_entity(math::v3 position, math::v3a rotation, const char* script_name);
void remove_game_entity(game_entity::entity_id id);

namespace
{
	const u64 left_set{ 0 };
	const u64 right_set{ 1 };

	utl::vector<graphics::light> lights;

	constexpr math::v3 rgb_to_color(u8 r, u8 g, u8 b) { return { r / 255.0f,g / 255.0f, b / 255.0f }; }

}	// 匿名名前空間

void generate_lights()
{
	// LEFT_SET
	graphics::light_init_info info{};
	info.entity_id = create_one_game_entity({}, { 0.0f, 0.0f, 0.0f }, nullptr).get_id();
	info.type = graphics::light::directional;
	info.light_set_key = left_set;
	info.intensity = 1.0f;
	info.color = rgb_to_color(174, 174, 174);

	lights.emplace_back(graphics::create_light(info));

	info.entity_id = create_one_game_entity({}, { math::pi * 0.5f, 0.0f, 0.0f }, nullptr).get_id();
	info.color = rgb_to_color(17, 27, 48);
	lights.emplace_back(graphics::create_light(info));

	info.entity_id = create_one_game_entity({}, { -math::pi * 0.5f, 0.0f, 0.0f }, nullptr).get_id();
	info.color = rgb_to_color(63, 47, 30);
	lights.emplace_back(graphics::create_light(info));

	// RIGHT_SET
	info.entity_id = create_one_game_entity({}, { 0.0f, 0.0f, 0.0f }, nullptr).get_id();
	info.light_set_key = right_set;
	info.color = rgb_to_color(150, 100, 200);
	lights.emplace_back(graphics::create_light(info));

	info.entity_id = create_one_game_entity({}, { math::pi * 0.5f, 0.0f, 0.0f }, nullptr).get_id();
	info.color = rgb_to_color(17, 27, 48);
	lights.emplace_back(graphics::create_light(info));

	info.entity_id = create_one_game_entity({}, { -math::pi * 0.5f, 0.0f, 0.0f }, nullptr).get_id();
	info.color = rgb_to_color(63, 47, 30);
	lights.emplace_back(graphics::create_light(info));
}

void remove_lights()
{
	for (auto& light : lights)
	{
		const game_entity::entity_id id{ light.entity_id() };
		graphics::remove_light(light.get_id(), light.light_set_key());
		remove_game_entity(id);
	}

	lights.clear();
}



