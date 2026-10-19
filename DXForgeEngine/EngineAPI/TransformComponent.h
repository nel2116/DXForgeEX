// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [TransformComponent.h]
// 作成日 : 2024/11/11
// 作成者 : 田中ミノル
// 概要
//  外部に公開するTransformComponentクラスを定義したファイル
// 更新履歴
// 2024/11/11 新規作成
// 2026/10/18 set_orientation()を削除し、向きを回転から導出するローカルフレーム(right/up/front)で保持するよう変更 (#4)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "../Components/ComponentsCommon.h"

namespace dxforge::transform
{

	DEFINE_TYPED_ID(transform_id);

	class component final
	{
	public:
		constexpr explicit component(transform_id id) : _id(id) {}
		constexpr component() : _id(id::invalid_id) {}
		/// <summary>
		/// TransformコンポーネントのIDを取得する
		/// </summary>
		/// <returns>TransformコンポーネントのID</returns>
		constexpr transform_id get_id() const { return _id; }
		/// <summary>
		/// Transformコンポーネントが有効かどうかを判定する
		/// </summary>
		/// <returns>有効ならtrue、無効ならfalse</returns>
		constexpr bool is_valid() const { return id::is_valid(_id); }

		math::v4 rotation() const;
		math::v3 position() const;
		math::v3 scale() const;
		/// <summary>
		/// 回転から導出される右方向(ローカルX軸)を取得する
		/// </summary>
		math::v3 right() const;
		/// <summary>
		/// 回転から導出される上方向(ローカルY軸)を取得する
		/// </summary>
		math::v3 up() const;
		/// <summary>
		/// 回転から導出される前方向(ローカルZ軸)を取得する
		/// </summary>
		math::v3 front() const;
		/// <summary>
		/// 右・上・前方向を行として並べたローカルフレームを取得する
		/// </summary>
		math::m3x3 local_frame() const;

	private:
		/// <summary>
		/// TransformコンポーネントのID
		/// </summary>
		transform_id _id;
	};

}