// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [TransformComponent.h]
// 作成日 : 2024/11/11
// 作成者 : 田中ミノル
// 概要
//  外部に公開するTransformComponentクラスを定義したファイル
// 更新履歴
// 2024/11/11 新規作成
// 2026/10/18 set_orientation()を削除し、向きを回転から導出するローカルフレーム(right/up/front)で保持するよう変更 (#4)
// 2026/11/08 座標系(space::frame)と、ローカル/ワールドの差分を適用した位置・回転を求める関数を追加 (Primal Day 086, #244)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "../Components/ComponentsCommon.h"

namespace dxforge::transform
{

	/// @brief 位置・回転を設定するときの値の意味
	struct space
	{
		enum frame : u32
		{
			absolute,	///< 値そのもの
			local,		///< エンティティのローカル軸(right/up/front)についての差分
			world,		///< ワールド軸についての差分

			count
		};
	};

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

		/// <summary>
		/// ローカル軸(right/up/front)に沿った移動量 delta を足した位置を求める(値は変えない)
		/// </summary>
		math::v3 calculate_local_position(math::v3 delta) const;
		/// <summary>
		/// 差分のクォータニオンをローカル軸について掛けた回転を求める(値は変えない)。結果は正規化する
		/// </summary>
		math::v4 calculate_local_rotation(math::v4 delta_quaternion) const;
		/// <summary>
		/// 差分のクォータニオンをワールド軸について掛けた回転を求める(値は変えない)。結果は正規化する
		/// </summary>
		math::v4 calculate_world_rotation(math::v4 delta_quaternion) const;

	private:
		/// <summary>
		/// TransformコンポーネントのID
		/// </summary>
		transform_id _id;
	};

}