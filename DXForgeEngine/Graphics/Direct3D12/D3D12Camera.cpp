// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Camera.cpp]
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
#include "D3D12Camera.h"
#include "EngineAPI/GameEntity.h"

namespace dxforge::graphics::d3d12::camera
{

	// ====== 匿名名前空間 ======
	namespace
	{
		// カメラの情報を格納するフリースト構造
		utl::free_list<d3d12_camera> cameras;

		/// @brief カメラの"上方向"ベクトルを設定する関数
		/// @param camera 設定対象のカメラ
		/// @param data "上方向"ベクトルのデータ
		/// @param size データサイズ
		void set_up_vector(d3d12_camera& camera, const void* const data, [[maybe_unused]] u32 size)
		{
			math::v3 up_vector{ *(math::v3*)data };
			assert(sizeof(up_vector) == size);
			camera.up(up_vector);
		}

		/// @brief カメラの視野角を設定する関数
		/// @param camera 設定対象のカメラ
		/// @param data 視野角のデータ
		/// @param size データサイズ
		constexpr void set_field_of_view(d3d12_camera& camera, const void* const data, [[maybe_unused]] u32 size)
		{
			assert(camera.projection_type() == graphics::camera::perspective);
			f32 fov{ *(f32*)data };
			assert(sizeof(fov) == size);
			camera.field_of_view(fov);
		}

		/// @brief カメラのアスペクト比を設定する関数
		/// @param camera 設定対象のカメラ
		/// @param data アスペクト比のデータ
		/// @param size データサイズ
		constexpr void set_aspect_ratio(d3d12_camera& camera, const void* const data, [[maybe_unused]] u32 size)
		{
			assert(camera.projection_type() == graphics::camera::perspective);
			f32 aspect_ratio{ *(f32*)data };
			assert(sizeof(aspect_ratio) == size);
			camera.aspect_ratio(aspect_ratio);
		}

		/// @brief カメラのビュー幅を設定する関数
		/// @param camera 設定対象のカメラ
		/// @param data ビュー幅のデータ
		/// @param size データサイズ
		constexpr void set_view_width(d3d12_camera& camera, const void* const data, [[maybe_unused]] u32 size)
		{
			assert(camera.projection_type() == graphics::camera::orthographic);
			f32 view_width{ *(f32*)data };
			assert(sizeof(view_width) == size);
			camera.view_width(view_width);
		}

		/// @brief カメラのビュー高さを設定する関数
		/// @param camera 設定対象のカメラ
		/// @param data ビュー高さのデータ
		/// @param size データサイズ
		constexpr void set_view_height(d3d12_camera& camera, const void* const data, [[maybe_unused]] u32 size)
		{
			assert(camera.projection_type() == graphics::camera::orthographic);
			f32 view_height{ *(f32*)data };
			assert(sizeof(view_height) == size);
			camera.view_height(view_height);
		}

		/// @brief カメラの近クリップ面を設定する関数
		/// @param camera 設定対象のカメラ
		/// @param data 近クリップ面のデータ
		/// @param size データサイズ
		constexpr void set_near_z(d3d12_camera& camera, const void* const data, [[maybe_unused]] u32 size)
		{
			f32 near_z{ *(f32*)data };
			assert(sizeof(near_z) == size);
			camera.near_z(near_z);
		}

		/// @brief カメラの遠クリップ面を設定する関数
		/// @param camera 設定対象のカメラ
		/// @param data 遠クリップ面のデータ
		/// @param size データサイズ
		constexpr void set_far_z(d3d12_camera& camera, const void* const data, [[maybe_unused]] u32 size)
		{
			f32 far_z{ *(f32*)data };
			assert(sizeof(far_z) == size);
			camera.far_z(far_z);
		}

		/// @brief カメラのビュー行列を取得する関数
		/// @param camera 取得対象のカメラ
		/// @param data ビュー行列のデータ
		/// @param size データサイズ
		void get_view(const d3d12_camera& camera, void* const data, [[maybe_unused]] u32 size)
		{
			math::m4x4* const matrix{ (math::m4x4* const)data };
			assert(sizeof(math::m4x4) == size);
			DirectX::XMStoreFloat4x4(matrix, camera.view());
		}

		/// @brief カメラの射影行列を取得する関数
		/// @param camera 取得対象のカメラ
		/// @param data 射影行列のデータ
		/// @param size データサイズ
		void get_projection(const d3d12_camera& camera, void* const data, [[maybe_unused]] u32 size)
		{
			math::m4x4* const matrix{ (math::m4x4* const)data };
			assert(sizeof(math::m4x4) == size);
			DirectX::XMStoreFloat4x4(matrix, camera.projection());
		}

		/// @brief カメラの逆射影行列を取得する関数
		/// @param camera 取得対象のカメラ
		/// @param data 逆射影行列のデータ
		/// @param size データサイズ
		void get_inverse_projection(const d3d12_camera& camera, void* const data, [[maybe_unused]] u32 size)
		{
			math::m4x4* const matrix{ (math::m4x4* const)data };
			assert(sizeof(math::m4x4) == size);
			DirectX::XMStoreFloat4x4(matrix, camera.inverse_projection());
		}

		/// @brief カメラのビュー射影行列を取得する関数
		/// @param camera 取得対象のカメラ
		/// @param data ビュー射影行列のデータ
		/// @param size データサイズ
		void get_view_projection(const d3d12_camera& camera, void* const data, [[maybe_unused]] u32 size)
		{
			math::m4x4* const matrix{ (math::m4x4* const)data };
			assert(sizeof(math::m4x4) == size);
			DirectX::XMStoreFloat4x4(matrix, camera.view_projection());
		}

		/// @brief カメラの逆ビュー射影行列を取得する関数
		/// @param camera 取得対象のカメラ
		/// @param data 逆ビュー射影行列のデータ
		/// @param size データサイズ
		void get_inverse_view_projection(const d3d12_camera& camera, void* const data, [[maybe_unused]] u32 size)
		{
			math::m4x4* const matrix{ (math::m4x4* const)data };
			assert(sizeof(math::m4x4) == size);
			DirectX::XMStoreFloat4x4(matrix, camera.inverse_view_projection());
		}

		/// @brief カメラの"上方向"ベクトルを取得する関数
		/// @param camera 取得対象のカメラ
		/// @param data "上方向"ベクトルのデータ
		/// @param size データサイズ
		void get_up_vector(const d3d12_camera& camera, void* const data, [[maybe_unused]] u32 size)
		{
			math::v3* const up_vector{ (math::v3* const)data };
			assert(sizeof(math::v3) == size);
			DirectX::XMStoreFloat3(up_vector, camera.up());
		}

		/// @brief カメラの視野角を取得する関数
		/// @param camera 取得対象のカメラ
		/// @param data 視野角のデータ
		/// @param size データサイズ
		constexpr void get_field_of_view(const d3d12_camera& camera, void* const data, [[maybe_unused]] u32 size)
		{
			assert(camera.projection_type() == graphics::camera::perspective);
			f32* const fov{ (f32* const)data };
			assert(sizeof(f32) == size);
			*fov = camera.field_of_view();
		}

		/// @brief カメラのアスペクト比を取得する関数
		/// @param camera 取得対象のカメラ
		/// @param data アスペクト比のデータ
		/// @param size データサイズ
		constexpr void get_aspect_ratio(const d3d12_camera& camera, void* const data, [[maybe_unused]] u32 size)
		{
			assert(camera.projection_type() == graphics::camera::perspective);
			f32* const aspect_ratio{ (f32* const)data };
			assert(sizeof(f32) == size);
			*aspect_ratio = camera.aspect_ratio();
		}

		/// @brief カメラのビュー幅を取得する関数
		/// @param camera 取得対象のカメラ
		/// @param data ビュー幅のデータ
		/// @param size データサイズ
		constexpr void get_view_width(const d3d12_camera& camera, void* const data, [[maybe_unused]] u32 size)
		{
			assert(camera.projection_type() == graphics::camera::orthographic);
			f32* const view_width{ (f32* const)data };
			assert(sizeof(f32) == size);
			*view_width = camera.view_width();
		}

		/// @brief カメラのビュー高さを取得する関数
		/// @param camera 取得対象のカメラ
		/// @param data ビュー高さのデータ
		/// @param size データサイズ
		constexpr void get_view_height(const d3d12_camera& camera, void* const data, [[maybe_unused]] u32 size)
		{
			assert(camera.projection_type() == graphics::camera::orthographic);
			f32* const view_height{ (f32* const)data };
			assert(sizeof(f32) == size);
			*view_height = camera.view_height();
		}

		/// @brief カメラの近クリップ面を取得する関数
		/// @param camera 取得対象のカメラ
		/// @param data 近クリップ面のデータ
		/// @param size データサイズ
		constexpr void get_near_z(const d3d12_camera& camera, void* const data, [[maybe_unused]] u32 size)
		{
			f32* const near_z{ (f32* const)data };
			assert(sizeof(f32) == size);
			*near_z = camera.near_z();
		}

		/// @brief カメラの遠クリップ面を取得する関数
		/// @param camera 取得対象のカメラ
		/// @param data 遠クリップ面のデータ
		/// @param size データサイズ
		constexpr void get_far_z(const d3d12_camera& camera, void* const data, [[maybe_unused]] u32 size)
		{
			f32* const far_z{ (f32* const)data };
			assert(sizeof(f32) == size);
			*far_z = camera.far_z();
		}

		/// @brief カメラのビュー行列を取得する関数
		/// @param camera 取得対象のカメラ
		/// @param data ビュー行列のデータ
		/// @param size データサイズ
		constexpr void get_projection_type(const d3d12_camera& camera, void* const data, [[maybe_unused]] u32 size)
		{
			graphics::camera::type* const type{ (graphics::camera::type* const)data };
			assert(sizeof(graphics::camera::type) == size);
			*type = camera.projection_type();
		}

		/// @brief カメラのエンティティIDを取得する関数
		/// @param camera 取得対象のカメラ
		/// @param data エンティティIDのデータ
		/// @param size データサイズ
		constexpr void get_entity_id(const d3d12_camera& camera, void* const data, [[maybe_unused]] u32 size)
		{
			id::id_type* const entity_id{ (id::id_type* const)data };
			assert(sizeof(id::id_type) == size);
			*entity_id = camera.entity_id();
		}

		constexpr void dummy_set(d3d12_camera&, const void* const, u32)
		{
		}

		// カメラのパラメータ設定関数
		using set_function = void(*)(d3d12_camera&, const void* const, u32);
		// カメラのパラメータ取得関数
		using get_function = void(*)(const d3d12_camera&, void* const, u32);
		// カメラのパラメータ設定関数配列
		constexpr set_function set_functions[]
		{
			set_up_vector,
			set_field_of_view,
			set_aspect_ratio,
			set_view_width,
			set_view_height,
			set_near_z,
			set_far_z,
			dummy_set,
			dummy_set,
			dummy_set,
			dummy_set,
			dummy_set,
			dummy_set,
			dummy_set,
		};

		static_assert(_countof(set_functions) == camera_parameter::count);

		// カメラのパラメータ取得関数配列
		constexpr get_function get_functions[]
		{
			get_up_vector,
			get_field_of_view,
			get_aspect_ratio,
			get_view_width,
			get_view_height,
			get_near_z,
			get_far_z,
			get_view,
			get_projection,
			get_inverse_projection,
			get_view_projection,
			get_inverse_view_projection,
			get_projection_type,
			get_entity_id,
		};

		static_assert(_countof(get_functions) == camera_parameter::count);

	} // 匿名名前空間

	// ====== クラス実装部 ======
	d3d12_camera::d3d12_camera(camera_init_info info)
		: _up{ DirectX::XMLoadFloat3(&info.up) },
		_near_z{ info.near_z }, _far_z{ info.far_z },
		_field_of_view{ info.field_of_view }, _aspect_ratio{ info.aspect_ratio },
		_projection_type{ info.type }, _entity_id{ info.entity_id }, _is_dirty{ true }
	{
		assert(id::is_valid(_entity_id));
		update();
	}

	/// @brief カメラを更新する関数
	void d3d12_camera::update()
	{
		game_entity::entity entity{ game_entity::entity_id{_entity_id} };
		using namespace DirectX;
		math::v3 pos{ entity.transform().position() };
		math::v3 dir{ entity.transform().orientation() };
		_position = XMLoadFloat3(&pos);
		_direction = XMLoadFloat3(&dir);
		_view = XMMatrixLookToRH(_position, _direction, _up);

		if (_is_dirty)
		{
			_projection = (_projection_type == graphics::camera::perspective)
				? XMMatrixPerspectiveFovRH(_field_of_view * XM_PI, _aspect_ratio, _far_z, _near_z)
				: XMMatrixOrthographicRH(_view_width, _view_height, _far_z, _near_z);
			_inverse_projection = XMMatrixInverse(nullptr, _projection);
			_is_dirty = false;
		}

		_view_projection = XMMatrixMultiply(_view, _projection);
		_inverse_view_projection = XMMatrixInverse(nullptr, _view_projection);
	}

	/// @brief カメラの"上方向"ベクトルを設定する関数
	/// @param up カメラの"上方向"ベクトル
	void d3d12_camera::up(math::v3 up)
	{
		_up = DirectX::XMLoadFloat3(&up);
	}

	/// @brief カメラの視野角を設定する関数
	/// @param fov 視野角
	constexpr void d3d12_camera::field_of_view(f32 fov)
	{
		assert(_projection_type == graphics::camera::perspective);
		_field_of_view = fov;
		_is_dirty = true;
	}

	/// @brief カメラのアスペクト比を設定する関数
	/// @param aspect_ratio アスペクト比
	constexpr void d3d12_camera::aspect_ratio(f32 aspect_ratio)
	{
		assert(_projection_type == graphics::camera::perspective);
		_aspect_ratio = aspect_ratio;
		_is_dirty = true;
	}

	/// @brief カメラのビュー幅を設定する関数
	/// @param width ビュー幅
	constexpr void d3d12_camera::view_width(f32 width)
	{
		assert(width);
		assert(_projection_type == graphics::camera::orthographic);
		_view_width = width;
		_is_dirty = true;
	}

	/// @brief カメラのビュー高さを設定する関数
	/// @param height ビュー高さ
	constexpr void d3d12_camera::view_height(f32 height)
	{
		assert(height);
		assert(_projection_type == graphics::camera::orthographic);
		_view_height = height;
		_is_dirty = true;
	}

	/// @brief カメラの近クリップ面を設定する関数
	/// @param near_z 近クリップ面
	constexpr void d3d12_camera::near_z(f32 near_z)
	{
		_near_z = near_z;
		_is_dirty = true;
	}

	/// @brief カメラの遠クリップ面を設定する関数
	/// @param far_z 遠クリップ面
	constexpr void d3d12_camera::far_z(f32 far_z)
	{
		_far_z = far_z;
		_is_dirty = true;
	}

	/// @brief カメラを生成する関数
	/// @param info カメラの初期化情報
	/// @return 生成したカメラ
	graphics::camera create(camera_init_info info)
	{
		return graphics::camera{ camera_id{ cameras.add(info) } };
	}

	/// @brief カメラを削除する関数
	/// @param id 削除対象のカメラID
	void remove(camera_id id)
	{
		assert(id::is_valid(id));
		cameras.remove(id);
	}

	/// @brief カメラのパラメータを設定する関数
	/// @param id 設定対象のカメラID
	/// @param parameter 設定するパラメータ
	/// @param data パラメータのデータ
	/// @param data_size データサイズ
	void set_parameter(camera_id id, camera_parameter::parameter parameter, const void* const data, u32 data_size)
	{
		assert(data && data_size);
		assert(parameter < camera_parameter::count);
		d3d12_camera& camera{ get(id) };
		set_functions[parameter](camera, data, data_size);
	}

	/// @brief カメラのパラメータを取得する関数
	/// @param id 取得対象のカメラID
	/// @param parameter 取得するパラメータ
	/// @param data パラメータのデータを格納するバッファ
	/// @param data_size データサイズ
	void get_parameter(camera_id id, camera_parameter::parameter parameter, void* const data, u32 data_size)
	{
		assert(data && data_size);
		assert(parameter < camera_parameter::count);
		d3d12_camera& camera{ get(id) };
		get_functions[parameter](camera, data, data_size);
	}

	/// @brief カメラを取得する関数
	/// @param id 取得対象のカメラID
	/// @return 取得したカメラ
	d3d12_camera& get(camera_id id)
	{
		assert(id::is_valid(id));
		return cameras[id];
	}

}	// namespace dxforge::graphics::d3d12::camera