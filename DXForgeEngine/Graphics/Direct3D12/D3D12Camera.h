// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Camera.h]
// 作成日 : 2025/01/12
// 作成者 : 田中ミノル
// 概要 :
// Direct3D12用カメラクラス
// 更新履歴
// 2025/01/12 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12CommonHeaders.h"

namespace dxforge::graphics::d3d12::camera
{
	class d3d12_camera
	{
	public:		// パブリック関数
		explicit d3d12_camera(camera_init_info info);

		void update();


		// ====== アクセサ ======
		void up(math::v3 up);
		void field_of_view(f32 fov);
		void aspect_ratio(f32 aspect_ratio);
		void view_width(f32 width);
		void view_height(f32 height);
		void near_z(f32 near_z);
		void far_z(f32 far_z);

		[[nodiscard]] constexpr DirectX::XMMATRIX view() const { return _view; };
		[[nodiscard]] constexpr DirectX::XMMATRIX projection() const { return _projection; };
		[[nodiscard]] constexpr DirectX::XMMATRIX inverse_projection() const { return _inverse_projection; };
		[[nodiscard]] constexpr DirectX::XMMATRIX view_projection() const { return _view_projection; };
		[[nodiscard]] constexpr DirectX::XMMATRIX inverse_view_projection() const { return _inverse_view_projection; };
		[[nodiscard]] constexpr DirectX::XMVECTOR up() const { return _up; };
		[[nodiscard]] constexpr f32 near_z() const { return _near_z; };
		[[nodiscard]] constexpr f32 far_z() const { return _far_z; };
		[[nodiscard]] constexpr f32 field_of_view() const { return _field_of_view; };
		[[nodiscard]] constexpr f32 aspect_ratio() const { return _aspect_ratio; };
		[[nodiscard]] constexpr f32 view_width() const { return _view_width; };
		[[nodiscard]] constexpr f32 view_height() const { return _view_height; };
		[[nodiscard]] constexpr graphics::camera::type projection_type() const { return _projection_type; };
		[[nodiscard]] constexpr id::id_type entity_id() const { return _entity_id; };

	private:	// メンバ変数
		DirectX::XMMATRIX _view;					// ビュー行列
		DirectX::XMMATRIX _projection;				// プロジェクション行列
		DirectX::XMMATRIX _inverse_projection;		// 逆プロジェクション行列
		DirectX::XMMATRIX _view_projection;			// ビュープロジェクション行列
		DirectX::XMMATRIX _inverse_view_projection;	// 逆ビュープロジェクション行列
		DirectX::XMVECTOR _up;						// 上方向
		f32 _near_z;								// 近クリップ面
		f32 _far_z;									// 遠クリップ面
		union
		{
			f32 _field_of_view;						// 視野角
			f32 _view_width;						// 正投影のビューの幅
		};
		union
		{
			f32 _aspect_ratio;						// アスペクト比
			f32 _view_height;						// 正投影のビューの高さ
		};
		graphics::camera::type _projection_type;	// プロジェクションタイプ
		id::id_type _entity_id;						// エンティティID
		bool _is_dirty;								// 更新フラグ
	};

	graphics::camera create(camera_init_info info);
	void remove(camera_id id);
	void set_parameter(camera_id id, camera_parameter::parameter param, const void* const data, u32 data_size);
	void get_parameter(camera_id id, camera_parameter::parameter param, void* const data, u32 data_size);
	[[nodiscard]] d3d12_camera& get(camera_id id);
}	// namespace dxforge::graphics::direct3d12


