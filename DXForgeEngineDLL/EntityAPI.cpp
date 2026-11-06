// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [EngineAPI.cpp]
// 作成日 : 2024/12/10
// 作成者 : 田中ミノル
// 概要 :
// エンティティのAPI
// 更新履歴
// 2024/12/10 新規作成
// 2026/11/03 Geometryコンポーネントの受け渡しを追加 (Primal Day 079, #32)
// 2026/11/06 回転を度数法で受け取り、ミューテックス、UpdateComponent、GetComponentId を追加 (Primal Day 082, #201)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Common.h"
#include "CommonHeaders.h"
#include "Id.h"
#include "Components/Entity.h"
#include "Components/Transform.h"
#include "Components/Script.h"
#include "Components/Geometry.h"

using namespace dxforge;

/// @brief オイラー角をクォータニオンにする
/// @param angles ピッチ、ヨー、ロール
/// @param is_degrees angles が度数法なら true(エディタの Transform の回転は度数法)
math::v4 to_quat(math::v3 angles, bool is_degrees)
{
	using namespace DirectX;
	if (is_degrees) angles = math::to_radians(angles);

	math::v4 quat_result{};
	const XMVECTOR quat{ XMQuaternionRotationRollPitchYawFromVector(XMLoadFloat3(&angles)) };
	XMStoreFloat4(&quat_result, quat);
	return quat_result;
}

namespace
{
	struct transform_component
	{
		f32 position[3];
		f32 rotation[3];
		f32 scale[3];

		transform::init_info to_init_info() const
		{
			transform::init_info info{};
			memcpy(&info.position[0], &position[0], sizeof(position));
			memcpy(&info.scale[0], &scale[0], sizeof(scale));

			// エディター側で定義されているrotationのf32[3](度数法のオイラー角)をエンジン側で使えるようにf32[4](クォータニオン)に変換
			const math::v3 rot{ &rotation[0] };
			const math::v4 rot_quat{ to_quat(rot, true) };
			memcpy(&info.rotation[0], &rot_quat.x, sizeof(info.rotation));
			return info;
		};

	};

	struct script_component
	{
		script::detail::script_creator script_creator;

		script::init_info to_init_info() const
		{
			script::init_info info{};
			info.script_creator = script_creator;
			return info;
		};
	};

	// NOTE: 並びはエディタの EngineAPIStructs.GeometryComponent(DllWrappers/EngineAPI.cs)と合わせる
	struct geometry_component
	{
		id::id_type		geometry_content_id;	// エディタがアップロードしたジオメトリのID(無効ならGeometryを作らない)
		u32				material_count;			// サブメッシュの数と同じ
		id::id_type*	material_ids;			// サブメッシュごとのマテリアルのID(エディタが確保し、呼び出しのあとで解放する)

		geometry::init_info to_init_info() const
		{
			geometry::init_info info{};
			info.geometry_content_id = geometry_content_id;
			info.material_count = material_count;
			info.material_ids = material_ids;
			return info;
		};
	};

	struct game_entity_descriptor
	{
		transform_component transform;
		script_component	script;
		geometry_component	geometry;
	};

	// エンティティIDからエンティティを取得する
	game_entity::entity entity_from_id(id::id_type id)
	{
		return game_entity::entity{ game_entity::entity_id{id} };
	};

}	// 匿名名前空間

// エンティティとビューポートの操作を守るミューテックス(EngineAPI.cpp と共有する)
std::mutex mutex{};

// ゲームエンティティを生成する
EDITOR_INTERFACE id::id_type CreateGameEntity(game_entity_descriptor* e)
{
	std::lock_guard lock{ mutex };
	assert(e);
	game_entity_descriptor& desc{ *e };
	transform::init_info transform_info{ desc.transform.to_init_info() };
	script::init_info script_info{ desc.script.to_init_info() };
	geometry::init_info geometry_info{ desc.geometry.to_init_info() };
	game_entity::entity_info entity_info
	{
		&transform_info,
		&script_info,
		id::is_valid(desc.geometry.geometry_content_id) ? &geometry_info : nullptr,
	};
	return game_entity::create(entity_info).get_id();
}

EDITOR_INTERFACE void RemoveGameEntity(id::id_type id)
{
	std::lock_guard lock{ mutex };
	assert(id::is_valid(id));
	game_entity::remove(game_entity::entity_id{ id });
}

// エンティティの Script か Geometry を作り直す。type は component_type::type。新しいコンポーネントを作れたら 1 を返す
EDITOR_INTERFACE u32 UpdateComponent(id::id_type entity_id, game_entity_descriptor* e, component_type::type type)
{
	std::lock_guard lock{ mutex };
	assert(id::is_valid(entity_id) && e && type != component_type::transform);
	game_entity_descriptor& desc{ *e };
	script::init_info script_info{ desc.script.to_init_info() };
	geometry::init_info geometry_info{ desc.geometry.to_init_info() };
	game_entity::entity_info entity_info
	{
		nullptr,
		&script_info,
		id::is_valid(desc.geometry.geometry_content_id) ? &geometry_info : nullptr,
	};
	return game_entity::update_component(game_entity::entity_id{ entity_id }, entity_info, type) ? 1 : 0;
}

// エンティティのコンポーネントのIDを返す。そのコンポーネントがなければ id::invalid_id
EDITOR_INTERFACE id::id_type GetComponentId(id::id_type entity_id, component_type::type type)
{
	std::lock_guard lock{ mutex };
	assert(id::is_valid(entity_id));
	const game_entity::entity entity{ entity_from_id(entity_id) };

	switch (type)
	{
	case component_type::transform: return entity.transform().get_id();
	case component_type::script: return entity.script().get_id();
	case component_type::geometry: return entity.geometry().get_id();
	default: return id::invalid_id;
	}
}