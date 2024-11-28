// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [GameEntity.h]
// 作成日 : 2024/08/12
// 作成者 : 田中ミノル
// 概要
// 　外部に公開するentityクラスを定義したファイル
// 更新履歴
// 2024/08/12 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "..\Components\ComponentsCommon.h"
#include "TransformComponent.h"
#include "ScriptComponent.h"

namespace dxforge
{
	namespace game_entity
	{
		DEFINE_TYPED_ID(entity_id);

		/// <summary>
		/// ゲームエンティティを表すクラス
		/// </summary>
		class entity
		{
		public:
			constexpr explicit entity(entity_id id) : _id(id) {}
			constexpr entity() : _id(id::invalid_id) {}
			/// <summary>
			/// ゲームエンティティの ID を取得する
			/// </summary>
			/// <returns>ゲームエンティティのID</returns>
			constexpr entity_id get_id() const { return _id; }
			/// <summary>
			/// ゲームエンティティが有効かどうかを判定する
			/// </summary>
			/// <returns>有効なら true、無効なら false</returns>
			constexpr bool is_valid() const { return id::is_valid(_id); }

			transform::component transform() const;
			script::component script() const;
		private:
			/// <summary>
			/// ゲームエンティティのID
			/// </summary>
			entity_id _id;
		};
	}	// namespace game_entity

	namespace script
	{
		class entity_script : public game_entity::entity
		{
		public:
			virtual ~entity_script() = default;
			virtual void begin_play() = 0;
			virtual void update(float) = 0;
		protected:
			constexpr explicit entity_script(game_entity::entity entity)
				: game_entity::entity{ entity.get_id() } {}
		};

		namespace detail
		{
			using script_ptr = std::unique_ptr<entity_script>;
			using script_creator = script_ptr(*)(game_entity::entity entity);

			u8 register_script(size_t, script_creator);

			template<class script_class>
			script_ptr create_script(game_entity entity)
			{
				assert(entity.is_valid());
				return std::make_unique<script_class>(entity);
			}
		}	// namespace detail

#define REGISTER_SCRIPT(TYPE)
		class TYPE;
		namespace
		{
			const u8 _reg_character_script{ dxforge::script::detail::register_script(std::hash<std::string>("character_script"), &dxforge::script::detail::create_script<character_script>) };
		}
	};	// namespace script
}	// namespace dxforge