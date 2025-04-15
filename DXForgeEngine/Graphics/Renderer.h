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
// 2025/01/13 add_material()、remove_material()の追加
// 2025/01/18 light関連の構造体、関数を追加
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "Platform/Window.h"
#include "EngineAPI/Camera.h"
#include "EngineAPI/Light.h"

namespace dxforge::graphics
{
	struct frame_info
	{
		id::id_type* render_item_ids{ nullptr };
		f32* thresholds{ nullptr };
		u64 light_set_key{ 0 };
		f32 last_frame_time{ 16.7f };
		f32 average_frame_time{ 16.7f };
		u32 render_item_count{ 0 };
		camera_id camera_id{ id::invalid_id };
	};

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
		void render(frame_info info) const;

	private:
		surface_id _id{ id::invalid_id };
	};

	struct render_surface
	{
		platform::window window{};
		dxforge::graphics::surface surface{};
	};

	struct directional_light_params {};
	struct point_light_params
	{
		math::v3 attenuation;
		f32 range;
	};

	struct spot_light_params
	{
		math::v3 attenuation;
		f32 range;
		// ラジアン単位のアンブラ角 [0, pi］
		f32 umbra;
		// ラジアン単位のペナンブラ角 [umbra, pi］
		f32 penumbra;
	};

	struct light_init_info
	{
		u64 light_set_key{ 0 };
		id::id_type entity_id{ id::invalid_id };
		light::type type{};
		f32 intensity{ 1.0f };
		math::v3 color{ 1.0f, 1.0f, 1.0f };
		union
		{
			directional_light_params directional_params;
			point_light_params point_params;
			spot_light_params spot_params;
		};
		bool is_enabled{ true };
	};

	struct light_parameter
	{
		enum parameter :u32
		{
			is_enabled,
			intensity,
			color,
			attenuation,
			range,
			umbra,
			penumbra,
			type,
			entity_id,

			count
		};
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
		f32 near_z{ 0.0f };							// 近クリップ面
		f32 far_z{ 0.0f };							// 遠クリップ面
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
			near_z = 0.01f;
			far_z = 1000.0f;
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
			near_z = 0.01f;
			far_z = 1000.0f;
		};
	};

	struct shader_flags
	{
		enum flags : u32
		{
			none = 0x00,
			vertex = 0x01,
			hull = 0x02,
			domain = 0x04,
			geometry = 0x08,
			pixel = 0x10,
			compute = 0x20,
			amplification = 0x40,
			mesh = 0x80,
		};
	};


	struct shader_type
	{
		enum type : u32
		{
			vertex = 0,
			hull,
			domain,
			geometry,
			pixel,
			compute,
			amplification,
			mesh,

			count
		};
	};


	struct material_type
	{
		enum type : u32
		{
			opaque,
			// transparent, unlit, clear_coat, cloth, skin, foliage, hair, etc...
			count
		};
	};

	struct material_surface
	{
		math::v4    base_color{ 1.f, 1.f, 1.f, 1.f };
		math::v3    emissive{ 0.f, 0.f, 0.f };
		f32         emissive_intensity{ 1.f };
		f32         ambient_occlusion{ 1.f };
		f32         metallic{ 0.f };
		f32         roughness{ 1.f };
	};

	struct material_init_info
	{
		id::id_type* texture_ids;
		material_surface    surface;
		material_type::type type;
		u32                 texture_count; // NOTE: textureはオプションなので、texture countは0、texture_idsはnullptrでもよい。
		id::id_type         shader_ids[shader_type::count]{ id::invalid_id, id::invalid_id, id::invalid_id, id::invalid_id, id::invalid_id, id::invalid_id, id::invalid_id, id::invalid_id };
	};

	struct primitive_topology
	{
		enum type : u32
		{
			point_list = 1,
			line_list,
			line_strip,
			triangle_list,
			triangle_strip,
			count
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

	void create_light_set(u64 light_set_key);
	void remove_light_set(u64 light_set_key);

	light create_light(light_init_info info);
	void remove_light(light_id id, u64 light_set_key);

	camera create_camera(camera_init_info info);
	void remove_camera(camera_id id);

	id::id_type add_submesh(const u8*& data);
	void remove_submesh(id::id_type id);

	id::id_type add_texture(const u8* const data);
	void remove_texture(id::id_type id);

	id::id_type add_material(const material_init_info& info);
	void remove_material(id::id_type id);

	id::id_type add_render_item(id::id_type entitiy_id, id::id_type geometry_content_id, u32 material_count, const id::id_type* const material_ids);
	void remove_render_item(id::id_type id);

}	// namespace dxforge::graphics
