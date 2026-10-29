// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Core.h]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要
// 　Direct3D12のコア
// 更新履歴
// 2024/12/27 新規作成
// 2026/10/29 画面外のサーフェスと読み戻し、WARPの選択、無人実行のデバッグレイヤーの扱いを追加 (#127)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12CommonHeaders.h"

namespace dxforge::graphics::d3d12
{
	namespace camera { class d3d12_camera; }

	struct d3d12_frame_info
	{
		const frame_info* info{ nullptr };					///< フレーム情報
		camera::d3d12_camera* camera{ nullptr };			///< カメラ
		D3D12_GPU_VIRTUAL_ADDRESS global_shader_data{ 0 };	///< シェーダデータ
		u32 surface_width{ 0 };								///< サーフェスの幅
		u32 surface_height{ 0 };							///< サーフェスの高さ
		id::id_type light_culling_id{ id::invalid_id };		///< ライトカリングID
		u32 frame_index{ 0 };								///< フレームインデックス
		f32 delta_time{ 16.7f };							///< デルタタイム
	};
}	// namespace dxforge::graphics::d3d12

namespace dxforge::graphics::d3d12::core
{
	bool initialize(const graphics_init_info& init_info);
	void shutdown(void);
	u32 flush_debug_messages(void);									// デバッグレイヤーのメッセージを標準エラーに出す
	[[nodiscard]] const char* adapter_name(void);					// 使っているアダプタの名前(UTF-8)

	/// @brief 安全にリリースする
	/// @tparam T リリースするオブジェクトの型
	/// @param ptr リリースするオブジェクト
	template<typename T>
	constexpr void release(T*& ptr)
	{
		if (ptr)
		{
			ptr->Release();
			ptr = nullptr;
		}
	}

	namespace detail
	{
		/// @brief 遅延リリース
		/// @param ptr リリースするオブジェクト
		void deferred_release(IUnknown* ptr);
	}	// namespace detail


	template<typename T>
	constexpr void deferred_release(T*& ptr)
	{
		if (ptr)
		{
			detail::deferred_release(ptr);
			ptr = nullptr;
		}
	}

	// ------ アクセサ ------
	[[nodiscard]] id3d12_device* const device();					// デバイス
	[[nodiscard]] descriptor_heap& rtv_heap();						// RTVヒープ
	[[nodiscard]] descriptor_heap& dsv_heap();						// DSVヒープ
	[[nodiscard]] descriptor_heap& srv_heap();						// SRVヒープ
	[[nodiscard]] descriptor_heap& uav_heap();						// UAVヒープ
	[[nodiscard]] constant_buffer& cbuffer();						// 定数バッファ
	[[nodiscard]] u32 current_frame_index();						// 現在のフレームインデックス
	void set_deferred_releases_flag();								// 遅延リリースフラグを設定

	[[nodiscard]] surface create_surface(platform::window window);	// サーフェスを作成
	[[nodiscard]] surface create_offscreen_surface(u32 width, u32 height);	// 画面外のサーフェスを作成
	bool read_back_surface(surface_id id, utl::vector<math::v4>& linear_rgba);	// 画面外のサーフェスの読み戻し
	void remove_surface(surface_id id);								// サーフェスを削除
	void resize_surface(surface_id id, u32 width, u32 height);		// サーフェスのリサイズ
	[[nodiscard]] u32 surface_width(surface_id id);					// サーフェスの幅
	[[nodiscard]] u32 surface_height(surface_id id);				// サーフェスの高さ
	void render_surface(surface_id id, frame_info info);			// サーフェスのレンダリング
}	// namespace dxforge::graphics
