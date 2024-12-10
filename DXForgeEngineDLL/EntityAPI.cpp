// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [EngineAPI.cpp]
// 作成日 : 2024/12/10
// 作成者 : 田中ミノル
// 概要 :
// エンティティのAPI
// 更新履歴
// 2024/12/10 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Common.h"
#include "CommonHeaders.h"
#include "Id.h"
#include "..\DXForgeEngine\Components\Entity.h"
#include "..\DXForgeEngine\Components\Transform.h"

using namespace dxforge;

namespace
{
	struct transform_component
	{
		f32 position[3];
		f32 rotation[3];
		f32 scale[3];

		transform::init_info to_init_info()
		{
			using namespace DirectX;
			transform::init_info info{};
			memcpy(&info.position[0], &position[0], sizeof(f32) * _countof(position));
			memcpy(&info.scale[0], &scale[0], sizeof(f32) * _countof(scale));

			// エディター側で定義されているrotationのf32[3](オイラー角)をエンジン側で使えるようにf32[4](クォータニオン)に変換
			XMFLOAT3A rot{ &rotation[0] };
			XMVECTOR quat{ XMQuaternionRotationRollPitchYawFromVector(XMLoadFloat3A(&rot)) };
			XMFLOAT4A rot_quat{};
			XMStoreFloat4A(&rot_quat, quat);
			memcpy(&info.rotation[0], &rot_quat.x, sizeof(f32) * _countof(info.rotation));
			return info;
		};

	};

	struct game_entity_descriptor
	{
		transform_component transform;
	};

	// エンティティIDからエンティティを取得する
	game_entity::entity entity_from_id(id::id_type id)
	{
		return game_entity::entity{ game_entity::entity_id{id} };
	};

}	// 匿名名前空間

// ゲームエンティティを生成する
EDITOR_INTERFACE id::id_type CreateGameEntity(game_entity_descriptor* e)
{
	assert(e);
	game_entity_descriptor& desc{ *e };
	transform::init_info transform_info{ desc.transform.to_init_info() };
	game_entity::entity_info entity_info
	{
		&transform_info,
	};
	return game_entity::create(entity_info).get_id();
}

EDITOR_INTERFACE void RemoveGameEntity(id::id_type id)
{
	assert(id::is_valid(id));
	game_entity::remove(game_entity::entity_id{ id });
}