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
// 2026/10/18 camera_init_infoから上方向を削除し、up_vectorを取得専用にした (#62)
// 2026/10/19 環境光(IBL)のライトの初期化情報 ambient_params を追加 (Primal Day 075)
// 2026/10/29 画面外のサーフェス、読み戻し、アダプタの選択(graphics_init_info)を追加 (#127)
// 2026/10/29 画面外のサーフェスの最小の大きさを追加 (#128 のレビュー)
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
		/// @brief 描画結果をCPUに読み戻す(画面外のサーフェスのみ。create_offscreen_surface()で作ったもの)
		/// @param linear_rgba 読み戻した画素(リニアのRGBA、左上から行ごと、width() * height() 個)
		/// @return 読み戻せたらtrue。ウィンドウのサーフェスではfalse
		bool read_back(utl::vector<math::v4>& linear_rgba) const;

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

	/// @brief 環境光(IBL)のテクスチャのID(add_texture()の戻り値)。
	/// @details 無名の共用体のメンバーなので既定値を持てない(light_init_info{}では0になる)。使わないテクスチャにはid::invalid_idを設定すること。
	///          どれかが無効なIDの場合、環境光は描画に使われない(直接光だけになる)。
	struct ambient_light_params
	{
		id::id_type diffuse_texture_id;		// 拡散光のIBL(irradiance map)のキューブマップ
		id::id_type specular_texture_id;	// 鏡面反射のIBLのキューブマップ(ミップmがラフネスm / 5)
		id::id_type brdf_lut_texture_id;	// 鏡面反射のIBLのBRDF LUT
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
			ambient_light_params ambient_params;
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
			up_vector,		// 取得専用 (上方向はエンティティの回転から決まる)
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

	/// @brief 使うアダプタ(GPU)の選び方
	struct adapter_type
	{
		enum type : u32
		{
			default_adapter,	// 性能の高い順に、最低限の機能レベルを満たす最初のアダプタ(これまでどおり)
			warp,				// ソフトウェアのラスタライザ(WARP)。GPUやドライバーに依らないので、描画結果の比較に使う
		};
	};

	struct graphics_init_info
	{
		adapter_type::type adapter{ adapter_type::default_adapter };
		// 無人実行(ヘッドレス)のときtrue。Debugビルドでも、デバッグレイヤーのメッセージでブレークしない
		// (デバッガがないと、ただのクラッシュになるため)。メッセージは flush_debug_messages() で標準エラーに出す。
		bool unattended{ false };
	};

	bool initialize(graphics_platform platform, const graphics_init_info& info = {});
	void shutdown();

	/// @brief 無人実行のとき、溜まったデバッグレイヤーのメッセージ(WARNING以上)を標準エラーに出して消す
	/// @return ERROR・CORRUPTIONのメッセージの数(Debugビルドでない、またはデバッグレイヤーがなければ0)
	u32 flush_debug_messages();

	/// @brief 使っているアダプタの名前(UTF-8)。初期化前は空
	const char* adapter_name();

	// コンパイルされたエンジンシェーダーの場所を、実行ファイルのパスから相対的に取得する。
	// このパスは、現在使用されているグラフィックスAPIのものです。
	const char* get_engine_shaders_path();

	// 指定されたプラットフォーム用にコンパイルされたエンジンシェーダーの場所を、実行ファイルのパスから相対的に取得します。
	// このパスは、現在使用されているグラフィックスAPIのものです。
	const char* get_engine_shaders_path(graphics_platform platform);

	surface create_surface(platform::window window);
	/// @brief 画面外のサーフェスの最小の幅・高さ(D3D12のライトカリングのタイルの大きさ。これより小さいと描けない)
	constexpr u32 min_offscreen_surface_size{ 16 };
	/// @brief ウィンドウ・スワップチェーンを持たない、画面外のサーフェスを作る(ヘッドレスの描画用)。remove_surface()で消す
	/// @return 作れなかったとき(大きさがmin_offscreen_surface_size未満、テクスチャを確保できないなど)は無効なサーフェス
	surface create_offscreen_surface(u32 width, u32 height);
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
