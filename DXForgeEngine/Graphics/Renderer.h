// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Renderer.h]
// 作成日 : 2024/12/20
// 作成者 : 田中ミノル
// 概要
// 　レンダラー
// 更新履歴
// 2024/12/20 新規作成
// 2025/01/12 add_submesh()、remove_submesh()の追加
// 2025/01/12 カメラ関連の関数を追加
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "Platform/Window.h"
#include "EngineAPI/Camera.h"

namespace dxforge::graphics
{
	DEFINE_TYPED_ID(surface_id);

	/// @brief サーフェス
	/// @details グラフィックをメインウィンドウに表示するためのクラス
	class surface
	{
	public:
		constexpr explicit surface(surface_id id) : _id{ id } {}
		constexpr surface() = default;
		constexpr surface_id get_id() const { return _id; }
		constexpr bool is_valid() const { return id::is_valid(_id); }

		void resize(u32 width, u32 height) const;
		u32 width() const;
		u32 height() const;
		void render() const;

	private:
		surface_id _id{ id::invalid_id };
	};

	struct render_surface
	{
		platform::window window{};
		surface surface{};
	};

	struct camera_parameter
	{
		enum parameter : u32
		{
			up_vector,
			field_of_view,
			aspect_ratio,
			view_width,
			view_height,
			near_z,
			far_z,
			view,
			projection,
			inverse_projection,
			view_projection,
			inverse_view_projection,
			type,
			entity_id,

			count
		};
	};

	struct camera_init_info
	{
		id::id_type entity_id{ id::invalid_id };	// カメラがアタッチされるエンティティのID
		camera::type type{};						// カメラの種類
		math::v3 up;								// カメラの上方向
		union
		{
			f32 field_of_view;						// 視野角
			f32 view_width;							// 視野の幅
		};
		union
		{
			f32 aspect_ratio;						// アスペクト比
			f32 view_height;						// 視野の高さ
		};
		f32 near_z;									// 近クリップ面
		f32 far_z;									// 遠クリップ面
	};

	struct perspective_camera_init_info : public camera_init_info
	{
		explicit perspective_camera_init_info(id::id_type id)
		{
			assert(id::is_valid(id));
			entity_id = id;
			type = camera::type::perspective;
			up = { 0.0f, 1.0f, 0.0f };
			field_of_view = 0.25f;
			aspect_ratio = 16.0f / 9.0f;
			near_z = 0.001f;
			far_z = 100000.0f;
		};
	};

	struct orthographic_camera_init_info : public camera_init_info
	{
		explicit orthographic_camera_init_info(id::id_type id)
		{
			assert(id::is_valid(id));
			entity_id = id;
			type = camera::type::orthographic;
			up = { 0.0f, 1.0f, 0.0f };
			view_width = 1920;
			view_height = 1080;
			near_z = 0.001f;
			far_z = 100000.0f;
		};
	};

	enum class graphics_platform
	{
		direct3d12 = 0,
		vulkan,
		opengl,
	};

	bool initialize(graphics_platform platform);
	void shutdown();

	// コンパイルされたエンジンシェーダーの場所を、実行ファイルのパスから相対的に取得する。
	// このパスは、現在使用されているグラフィックスAPIのものです。
	const char* get_engine_shaders_path();

	// 指定されたプラットフォーム用にコンパイルされたエンジンシェーダーの場所を、実行ファイルのパスから相対的に取得します。
	// このパスは、現在使用されているグラフィックスAPIのものです。
	const char* get_engine_shaders_path(graphics_platform platform);

	surface create_surface(platform::window window);
	void remove_surface(surface_id id);

	camera create_camera(camera_init_info info);
	void remove_camera(camera_id id);

	id::id_type add_submesh(const u8*& data);
	void remove_submesh(id::id_type id);
}	// namespace dxforge::graphics