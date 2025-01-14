// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [GameEntity.h]
// 作成日 : 2024/08/12
// 作成者 : 田中ミノル
// 概要
// 　外部に公開するentityクラスを定義したファイル
// 更新履歴
// 2024/08/12 新規作成
// 2024/12/10 スクリプトコンポーネントの作成関数の追加
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "../Components/ComponentsCommon.h"
#include "TransformComponent.h"
#include "ScriptComponent.h"

namespace dxforge
{
	namespace game_entity
	{
		// ゲームエンティティのIDを定義
		DEFINE_TYPED_ID(entity_id);

		/// @brief ゲームエンティティを表すクラス
		/// @details ゲームエンティティは、ゲーム内のオブジェクトを表すクラス
		class entity
		{
		public:		// パブリック関数
			constexpr explicit entity(entity_id id) : _id(id) {}
			constexpr entity() : _id(id::invalid_id) {}


			// ------ アクセサ関数 ------
			/// @brief ゲームエンティティのIDを取得する
			/// @return ゲームエンティティのID
			[[nodiscard]] constexpr entity_id get_id() const { return _id; }
			/// @brief ゲームエンティティが有効かどうかを判定する
			/// @return 有効ならtrue, そうでなければfalse
			[[nodiscard]] constexpr bool is_valid() const { return id::is_valid(_id); }
			/// @brief ゲームエンティティのTransformコンポーネントを取得する
			/// @return Transformコンポーネント
			[[nodiscard]] transform::component transform() const;
			/// @brief ゲームエンティティのScriptコンポーネントを取得する
			/// @return Scriptコンポーネント
			[[nodiscard]] script::component script() const;
			/// @brief ゲームエンテティの回転値を取得する
			/// @return 回転値
			[[nodiscard]] math::v4 rotation() const { return transform().rotation(); }
			/// @brief ゲームエンテティの向きを取得する
			/// @return 向き
			[[nodiscard]] math::v3 orientation() const { return transform().orientation(); }
			/// @brief ゲームエンテティの位置を取得する
			/// @return 位置
			[[nodiscard]] math::v3 position() const { return transform().position(); }
			/// @brief ゲームエンテティの拡大率を取得する
			/// @return 拡大率
			[[nodiscard]] math::v3 scale() const { return transform().scale(); }

		private:	// メンバ変数
			entity_id _id;	///< ゲームエンティティのID
		};
	}	// namespace game_entity

	namespace script
	{
		/// @brief スクリプトを表すクラス
		/// @details スクリプトは、ゲームエンティティにアタッチされるスクリプトを表すクラス
		class entity_script :public game_entity::entity
		{
		public:		// パブリック関数
			virtual ~entity_script() = default;
			virtual void begin_play() {}
			virtual void update(float) {}

		protected:	// プロテクト関数
			constexpr explicit entity_script(game_entity::entity entity)
				: game_entity::entity{ entity.get_id() }{}

			void set_rotation(math::v4 rotation_quaternion) const { set_rotation(this, rotation_quaternion); }
			void set_orientation(math::v3 orientation_vector) const { set_orientation(this, orientation_vector); }
			void set_position(math::v3 position) const { set_position(this, position); }
			void set_scale(math::v3 scale) const { set_scale(this, scale); }

			static void set_rotation(const game_entity::entity* const entity, math::v4 rotation_quaternion);
			static void set_orientation(const game_entity::entity* const entity, math::v3 orientation_venctor);
			static void set_position(const game_entity::entity* const entity, math::v3 position);
			static void set_scale(const game_entity::entity* const entity, math::v3 scale);
		};

		namespace detail
		{
			using script_ptr = std::unique_ptr<entity_script>;					// スクリプトポインタ
			using script_creator = script_ptr(*)(game_entity::entity entity);	// スクリプト生成関数
			using string_hash = std::hash<std::string>;							// 文字列ハッシュ関数

			/// @brief スクリプトを登録する
			/// @param tag スクリプトのタグ
			/// @param creator スクリプト生成関数
			/// @return スクリプトのタグ
			u8 register_script(size_t, script_creator);

#ifdef USE_WITH_EDITOR
			extern "C" __declspec(dllexport)
#endif // USE_WITH_EDITOR

				/// @brief スクリプトを生成する
				/// @param tag スクリプトのタグ
				/// @return スクリプトポインタ
				script_creator get_script_creator(size_t tag);

			/// @brief スクリプトを生成する
			/// @tparam script_class スクリプトクラス
			template<class script_class>
			script_ptr create_script(game_entity::entity entity)
			{
				assert(entity.is_valid());
				return std::make_unique<script_class>(entity);
			}

#ifdef USE_WITH_EDITOR
			/// @brief スクリプト名を追加する
			/// @param name スクリプト名
			u8 add_script_name(const char* name);



#define REGISTER_SCRIPT(TYPE)                                   \
		namespace{                                              \
		const u8 _reg##TYPE                                     \
		{ dxforge::script::detail::register_script(             \
			dxforge::script::detail::string_hash()( #TYPE ),    \
			&dxforge::script::detail::create_script<TYPE>)};    \
		const u8 _name_##TYPE									\
		{ dxforge::script::detail::add_script_name( #TYPE ) };	\
		}

#else
#define REGISTER_SCRIPT(TYPE)                                   \
		namespace{                                              \
		const u8 _reg##TYPE                                     \
		{ dxforge::script::detail::register_script(             \
			dxforge::script::detail::string_hash()( #TYPE ),    \
			&dxforge::script::detail::create_script<TYPE>)};    \
		}
#endif // USE_WITH_EDITOR

		}	// namespace detail
	}	// namespace script
}	// namespace dxforge