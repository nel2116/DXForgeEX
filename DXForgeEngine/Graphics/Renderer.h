// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Renderer.h]
// 作成日 : 2024/12/20
// 作成者 : 田中ミノル
// 概要
// 　レンダラー
// 更新履歴
// 2024/12/20 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "..\Platform\Window.h"


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
}