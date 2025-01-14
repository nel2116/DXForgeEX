// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Camera.h]
// 作成日 : 2025/01/12
// 作成者 : 田中ミノル
// 概要 :
// Direct3D12用カメラクラス
// 更新履歴
// 2025/01/12 新規作成
// 2025/01/14 コメントの追加
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12CommonHeaders.h"

namespace dxforge::graphics::d3d12::camera
{
	/// @brief Direct3D12カメラを管理するクラス
	class d3d12_camera
	{
	public:		// パブリック関数
		/// @brief コンストラクタ
		/// @param info 初期化情報
		explicit d3d12_camera(camera_init_info info);

		/// @brief カメラを更新する
		void update();
		/// @brief 上方向ベクトルを設定する
		/// @param up 新しい上方向ベクトル
		void up(math::v3 up);
		/// @brief 視野角を設定する (パースペクティブカメラ用)
		/// @param fov 新しい視野角
		void field_of_view(f32 fov);
		/// @brief アスペクト比を設定する (パースペクティブカメラ用)
		/// @param aspect_ratio 新しいアスペクト比
		void aspect_ratio(f32 aspect_ratio);
		/// @brief ビュー幅を設定する (オーソグラフィックカメラ用)
		/// @param width 新しいビュー幅
		void view_width(f32 width);
		/// @brief ビュー高さを設定する (オーソグラフィックカメラ用)
		/// @param height 新しいビュー高さ
		void view_height(f32 height);
		/// @brief 近クリッピング平面を設定する
		/// @param near_z 新しい近クリッピング平面距離
		void near_z(f32 near_z);
		/// @brief 遠クリッピング平面を設定する
		/// @param far_z 新しい遠クリッピング平面距離
		void far_z(f32 far_z);

		// ------ アクセサ関数 ------
		[[nodiscard]] constexpr DirectX::XMMATRIX view() const { return _view; }
		[[nodiscard]] constexpr DirectX::XMMATRIX projection() const { return _projection; }
		[[nodiscard]] constexpr DirectX::XMMATRIX inverse_projection() const { return _inverse_projection; }
		[[nodiscard]] constexpr DirectX::XMMATRIX view_projection() const { return _view_projection; }
		[[nodiscard]] constexpr DirectX::XMMATRIX inverse_view_projection() const { return _inverse_view_projection; }
		[[nodiscard]] constexpr DirectX::XMVECTOR position() const { return _position; }
		[[nodiscard]] constexpr DirectX::XMVECTOR direction() const { return _direction; }
		[[nodiscard]] constexpr DirectX::XMVECTOR up() const { return _up; }
		[[nodiscard]] constexpr f32 near_z() const { return _near_z; }
		[[nodiscard]] constexpr f32 far_z() const { return _far_z; }
		[[nodiscard]] constexpr f32 field_of_view() const { return _field_of_view; }
		[[nodiscard]] constexpr f32 aspect_ratio() const { return _aspect_ratio; }
		[[nodiscard]] constexpr f32 view_width() const { return _view_width; }
		[[nodiscard]] constexpr f32 view_height() const { return _view_height; }
		[[nodiscard]] constexpr graphics::camera::type projection_type() const { return _projection_type; }
		[[nodiscard]] constexpr id::id_type entity_id() const { return _entity_id; }

	private:	// プライベート変数
		DirectX::XMMATRIX _view;					///< ビュー行列
		DirectX::XMMATRIX _projection;				///< プロジェクション行列
		DirectX::XMMATRIX _inverse_projection;		///< 逆プロジェクション行列
		DirectX::XMMATRIX _view_projection;			///< ビュープロジェクション行列
		DirectX::XMMATRIX _inverse_view_projection;	///< 逆ビュープロジェクション行列
		DirectX::XMVECTOR _position{};				///< カメラの位置
		DirectX::XMVECTOR _direction{};				///< カメラの向き
		DirectX::XMVECTOR _up;						///< 上方向ベクトル
		f32 _near_z;								///< 近クリッピング平面距離
		f32 _far_z;									///< 遠クリッピング平面距離
		union
		{
			f32 _field_of_view;						///< 視野角 (パースペクティブカメラ用)
			f32 _view_width;						///< ビュー幅 (オーソグラフィックカメラ用)
		};
		union
		{
			f32 _aspect_ratio;						///< アスペクト比 (パースペクティブカメラ用)
			f32 _view_height;						///< ビュー高さ (オーソグラフィックカメラ用)
		};
		graphics::camera::type _projection_type;	///< プロジェクションの種類
		id::id_type _entity_id;						///< 関連付けられたentity id
		bool _is_dirty;								///< 状態が変更されたかを示すフラグ
	};

	/// @brief カメラを生成する関数
	/// @param info 初期化情報
	/// @return 生成されたカメラ
	graphics::camera create(camera_init_info info);

	/// @brief カメラを削除する関数
	/// @param id 削除対象のカメラID
	void remove(camera_id id);

	/// @brief カメラのパラメータを設定
	/// @param id 対象カメラのID
	/// @param parameter 設定するパラメータの種類
	/// @param data パラメータデータ
	/// @param data_size データサイズ
	void set_parameter(camera_id id, camera_parameter::parameter parameter, const void* const data, u32 data_size);

	/// @brief カメラのパラメータを取得
	/// @param id 対象カメラのID
	/// @param parameter 取得するパラメータの種類
	/// @param data 出力先データ
	/// @param data_size データサイズ
	void get_parameter(camera_id id, camera_parameter::parameter parameter, void* const data, u32 data_size);

	/// @brief カメラを取得
	/// @param id 対象カメラのID
	/// @return カメラ参照
	[[nodiscard]] d3d12_camera& get(camera_id id);
}	// namespace dxforge::graphics::direct3d12


