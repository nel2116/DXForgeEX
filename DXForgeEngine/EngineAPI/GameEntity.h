// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [GameEntity.h]
// 作成日 : 2024/08/12
// 作成者 : 田中ミノル
// 概要
// 　外部に公開するentityクラスを定義したファイル
// 更新履歴
// 2024/08/12 新規作成
// 2024/12/10 スクリプトコンポーネントの作成関数の追加
// 2025/02/10 Geometryコンポーネントの作成関数の追加
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "../Components/ComponentsCommon.h"
#include "TransformComponent.h"
#include "ScriptComponent.h"
#include "GeometryComponent.h"

namespace dxforge::game_entity
{
	// ゲームエンティティのIDを定義
	DEFINE_TYPED_ID(entity_id);

	// ゲームエンティティクラス
	class entity
	{
	public:
		constexpr explicit entity(entity_id id) : _id{ id } {}
		constexpr entity() : _id{ id::invalid_id } {}
		[[nodiscard]] constexpr entity_id get_id() const { return _id; }
		[[nodiscard]] constexpr bool is_valid() const { return id::is_valid(_id); }

		[[nodiscard]] transform::component transform() const;
		[[nodiscard]] script::component script() const;
		[[nodiscard]] geometry::component geometry() const;

		[[nodiscard]] math::v4 rotation() const { return transform().rotation(); }
		[[nodiscard]] math::v3 orientation() const { return transform().orientation(); }
		[[nodiscard]] math::v3 position() const { return transform().position(); }
		[[nodiscard]] math::v3 scale() const { return transform().scale(); }

	private:
		entity_id _id;	// ゲームエンティティのID
	};
} // namespace game_entity

namespace dxforge::script
{
	// スクリプトクラス
	class entity_script : public game_entity::entity
	{
	public:
		virtual ~entity_script() = default;
		virtual void begin_play() {}
		virtual void update(f32) {}
	protected:
		constexpr explicit entity_script(game_entity::entity entity)
			: game_entity::entity{ entity.get_id() } {}

		void set_rotation(math::v4 rotation_quaternion) const { set_rotation(this, rotation_quaternion); }
		void set_orientation(math::v3 orientation_vector) const { set_orientation(this, orientation_vector); }
		void set_position(math::v3 position) const { set_position(this, position); }
		void set_scale(math::v3 scale) const { set_scale(this, scale); }

		static void set_rotation(const game_entity::entity* const entity, math::v4 rotation_quaternion);
		static void set_orientation(const game_entity::entity* const entity, math::v3 orientation_vector);
		static void set_position(const game_entity::entity* const entity, math::v3 position);
		static void set_scale(const game_entity::entity* const entity, math::v3 scale);
	};

	namespace detail
	{
		using script_ptr = std::unique_ptr<entity_script>;
		using script_creator = script_ptr(*)(game_entity::entity entity);
		using string_hash = std::hash<std::string>;

		u8 register_script(size_t, script_creator);
#ifdef USE_WITH_EDITOR
		extern "C" __declspec(dllexport)
#endif //USE_WITH_EDITOR
			script_creator get_script_creator(size_t tag);

		template<class script_class>
		script_ptr create_script(game_entity::entity entity)
		{
			assert(entity.is_valid());
			return std::make_unique<script_class>(entity);
		}

#ifdef USE_WITH_EDITOR
		u8 add_script_name(const char* name);

#define REGISTER_SCRIPT(TYPE)                                           \
        namespace {                                                     \
        const u8 _reg_##TYPE                                            \
        { dxforge::script::detail::register_script(                      \
              dxforge::script::detail::string_hash()(#TYPE),             \
              &dxforge::script::detail::create_script<TYPE>) };          \
        const u8 _name_##TYPE                                           \
        { dxforge::script::detail::add_script_name(#TYPE) };             \
        }

#else
#define REGISTER_SCRIPT(TYPE)                                           \
        namespace {                                                     \
        const u8 _reg_##TYPE                                            \
        { dxforge::script::detail::register_script(                      \
              dxforge::script::detail::string_hash()(#TYPE),             \
              &dxforge::script::detail::create_script<TYPE>) };          \
        }

#endif // USE_WITH_EDITOR
	} // namespace detail

}	// namespace dxforge::script