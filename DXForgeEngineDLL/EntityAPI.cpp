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
// 2026/11/07 ミューテックスを ticket_mutex にし、UpdateComponent の戻り値を b32 にした (Primal Day 083, #209)
// 2026/11/08 消すジオメトリの ID をビューポートから取り除くようにした (#214)
// 2026/11/08 エンティティの回転をクォータニオンで受け取り、オイラー角の変換をやめた (#205)
// 2026/11/08 作成後のエンティティの位置・回転・拡大率を読み書きする Get/Set Position/Rotation/Scale を追加 (Primal Day 086, #244)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Common.h"
#include "CommonHeaders.h"
#include "Id.h"
#include "Components/Entity.h"
#include "Components/Transform.h"
#include "Components/Script.h"
#include "Components/Geometry.h"
#include "Utilities/Threading.h"

using namespace dxforge;

/// @brief オイラー角をクォータニオンにする(エディタのカメラが使う。エンティティの回転はエディタがクォータニオンにして渡す)
/// @param angles ピッチ、ヨー、ロール
/// @param is_degrees angles が度数法なら true
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
	// NOTE: 並びはエディタの EngineAPIStructs.TransformComponent(DllWrappers/EngineAPI.cs)と合わせる
	//       Primal はここで度数法のオイラー角をクォータニオンにするが、DXForgeEX はエディタがクォータニオンを持つ
	//       (docs/design/rotation-units.md、#205)
	struct transform_component
	{
		f32 position[3];
		f32 rotation[4];	// クォータニオン(x, y, z, w)
		f32 scale[3];

		transform::init_info to_init_info() const
		{
			transform::init_info info{};
			memcpy(&info.position[0], &position[0], sizeof(position));
			static_assert(sizeof(info.rotation) == sizeof(rotation));
			memcpy(&info.rotation[0], &rotation[0], sizeof(rotation));
			memcpy(&info.scale[0], &scale[0], sizeof(scale));
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

	// エディタから受け取った ID のエンティティが生きているか。無効な ID や削除済みのエンティティの ID は、
	// エディタが持ち得る(非アクティブなエンティティなど)ので assert しない
	bool is_alive_entity(id::id_type id)
	{
		return id::is_valid(id) && game_entity::is_alive(game_entity::entity_id{ id });
	}

	// NOTE: Transform の ID はエンティティの ID と同じ(Transform.cpp の create())
	transform::component transform_from_id(id::id_type id)
	{
		assert(transform::transform_id{ id } == entity_from_id(id).transform().get_id());
		return transform::component{ transform::transform_id{ id } };
	}

	// Set* が transform::update() に渡すキャッシュ(mutex の中だけで使う)
	utl::vector<transform::component_cache> transform_cache{};

}	// 匿名名前空間

// エンティティとビューポートの操作を守るミューテックス(EngineAPI.cpp と共有する)
utl::ticket_mutex mutex{};

void remove_geometry_from_viewports(id::id_type geometry_id);	// EngineAPI.cpp(#214)

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
	remove_geometry_from_viewports(entity_from_id(id).geometry().get_id());
	game_entity::remove(game_entity::entity_id{ id });
}

// エンティティの Script か Geometry を作り直す。type は component_type::type。新しいコンポーネントを作れたら 1 を返す
EDITOR_INTERFACE b32 UpdateComponent(id::id_type entity_id, game_entity_descriptor* e, component_type::type type)
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
	if (type == component_type::geometry) remove_geometry_from_viewports(entity_from_id(entity_id).geometry().get_id());
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

// NOTE: 以下の Get*/Set* は Primal Day 086 の EntityAPI.cpp にあたる。Primal と違い、回転はいつもクォータニオン(x, y, z, w)で受け渡し、
//       DLL でオイラー角に変換しない(docs/design/rotation-units.md、#205、#244)。
//       どれも ids と各成分の配列を count 個受け取る(複数選択)。無効な ID と削除済みのエンティティの ID は飛ばして 0 を返し、
//       ほかの ID は処理する。Get* は飛ばした ID の出力を書かない。すべて処理できたら 1 を返す。count が 0 なら何もせず 1 を返す。
//       どれも mutex を 1 回だけ取る。

// エンティティの位置を読む
EDITOR_INTERFACE b32 GetPosition(const id::id_type* ids, f32* x, f32* y, f32* z, u32 count)
{
	if (!count) return 1;
	assert(ids && x && y && z);
	if (!(ids && x && y && z)) return 0;
	std::lock_guard lock{ mutex };

	b32 all_alive{ 1 };
	for (u32 i{ 0 }; i < count; ++i)
	{
		if (!is_alive_entity(ids[i])) { all_alive = 0; continue; }
		const math::v3 position{ transform_from_id(ids[i]).position() };
		x[i] = position.x; y[i] = position.y; z[i] = position.z;
	}
	return all_alive;
}

// エンティティの回転をクォータニオン(x, y, z, w)で読む。保存している値をそのまま返す
EDITOR_INTERFACE b32 GetRotation(const id::id_type* ids, f32* x, f32* y, f32* z, f32* w, u32 count)
{
	if (!count) return 1;
	assert(ids && x && y && z && w);
	if (!(ids && x && y && z && w)) return 0;
	std::lock_guard lock{ mutex };

	b32 all_alive{ 1 };
	for (u32 i{ 0 }; i < count; ++i)
	{
		if (!is_alive_entity(ids[i])) { all_alive = 0; continue; }
		const math::v4 rotation{ transform_from_id(ids[i]).rotation() };
		x[i] = rotation.x; y[i] = rotation.y; z[i] = rotation.z; w[i] = rotation.w;
	}
	return all_alive;
}

// エンティティの拡大率を読む
EDITOR_INTERFACE b32 GetScale(const id::id_type* ids, f32* x, f32* y, f32* z, u32 count)
{
	if (!count) return 1;
	assert(ids && x && y && z);
	if (!(ids && x && y && z)) return 0;
	std::lock_guard lock{ mutex };

	b32 all_alive{ 1 };
	for (u32 i{ 0 }; i < count; ++i)
	{
		if (!is_alive_entity(ids[i])) { all_alive = 0; continue; }
		const math::v3 scale{ transform_from_id(ids[i]).scale() };
		x[i] = scale.x; y[i] = scale.y; z[i] = scale.z;
	}
	return all_alive;
}

// エンティティの位置を設定する。frame は transform::space::frame
//   absolute: (x, y, z) を位置にする / local: エンティティのローカル軸(right/up/front)に沿って (x, y, z) だけ動かす /
//   world: ワールド軸に沿って (x, y, z) だけ動かす(Primal にはない、#244)
EDITOR_INTERFACE b32 SetPosition(const id::id_type* ids, const f32* x, const f32* y, const f32* z, u32 count, u32 frame)
{
	if (!count) return 1;
	assert(ids && x && y && z && frame < transform::space::count);
	if (!(ids && x && y && z) || frame >= transform::space::count) return 0;
	std::lock_guard lock{ mutex };

	transform_cache.clear();
	b32 all_alive{ 1 };
	for (u32 i{ 0 }; i < count; ++i)
	{
		if (!is_alive_entity(ids[i])) { all_alive = 0; continue; }
		const transform::component t{ transform_from_id(ids[i]) };
		const math::v3 v{ x[i], y[i], z[i] };
		transform::component_cache c{};
		c.id = t.get_id();
		c.flags = transform::component_flags::position;
		switch (frame)
		{
		case transform::space::local: c.position = t.calculate_local_position(v); break;
		case transform::space::world:
		{
			const math::v3 p{ t.position() };
			c.position = { p.x + v.x, p.y + v.y, p.z + v.z };
			break;
		}
		default: c.position = v; break;
		}
		transform_cache.emplace_back(c);
	}

	if (!transform_cache.empty()) transform::update(transform_cache.data(), (u32)transform_cache.size());
	return all_alive;
}

// エンティティの回転をクォータニオン(x, y, z, w)で設定する。frame は transform::space::frame
//   absolute: その回転にする / local: 差分をエンティティのローカル軸について掛ける / world: 差分をワールド軸について掛ける
//   結果は正規化する。差分のオイラー角や軸と角度はエディタがクォータニオンにする(rotation-units.md の順序)
EDITOR_INTERFACE b32 SetRotation(const id::id_type* ids, const f32* x, const f32* y, const f32* z, const f32* w, u32 count, u32 frame)
{
	if (!count) return 1;
	assert(ids && x && y && z && w && frame < transform::space::count);
	if (!(ids && x && y && z && w) || frame >= transform::space::count) return 0;
	std::lock_guard lock{ mutex };

	transform_cache.clear();
	b32 all_alive{ 1 };
	for (u32 i{ 0 }; i < count; ++i)
	{
		if (!is_alive_entity(ids[i])) { all_alive = 0; continue; }
		const transform::component t{ transform_from_id(ids[i]) };
		const math::v4 q{ x[i], y[i], z[i], w[i] };
		transform::component_cache c{};
		c.id = t.get_id();
		c.flags = transform::component_flags::rotation;
		switch (frame)
		{
		case transform::space::local: c.rotation = t.calculate_local_rotation(q); break;
		case transform::space::world: c.rotation = t.calculate_world_rotation(q); break;
		default:
		{
			using namespace DirectX;
			XMStoreFloat4(&c.rotation, XMQuaternionNormalize(XMLoadFloat4(&q)));
			break;
		}
		}
		transform_cache.emplace_back(c);
	}

	if (!transform_cache.empty()) transform::update(transform_cache.data(), (u32)transform_cache.size());
	return all_alive;
}

// エンティティの拡大率を設定する
EDITOR_INTERFACE b32 SetScale(const id::id_type* ids, const f32* x, const f32* y, const f32* z, u32 count)
{
	if (!count) return 1;
	assert(ids && x && y && z);
	if (!(ids && x && y && z)) return 0;
	std::lock_guard lock{ mutex };

	transform_cache.clear();
	b32 all_alive{ 1 };
	for (u32 i{ 0 }; i < count; ++i)
	{
		if (!is_alive_entity(ids[i])) { all_alive = 0; continue; }
		transform::component_cache c{};
		c.id = transform_from_id(ids[i]).get_id();
		c.flags = transform::component_flags::scale;
		c.scale = { x[i], y[i], z[i] };
		transform_cache.emplace_back(c);
	}

	if (!transform_cache.empty()) transform::update(transform_cache.data(), (u32)transform_cache.size());
	return all_alive;
}
