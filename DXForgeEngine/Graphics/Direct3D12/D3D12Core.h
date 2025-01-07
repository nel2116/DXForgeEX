// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Core.h]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要
// 　Direct3D12のコア
// 更新履歴
// 2024/12/27 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12CommonHeaders.h"


namespace dxforge::graphics::d3d12
{
	struct d3d12_frame_info
	{
		u32 surface_width{};
		u32 surface_height{};
	};
}	// namespace dxforge::graphics::d3d12

namespace dxforge::graphics::d3d12::core
{
	bool initialize(void);
	void shutdown(void);

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
	id3d12_device* const device();								// デバイス
	descriptor_heap& rtv_heap();								// RTVヒープ
	descriptor_heap& dsv_heap();								// DSVヒープ
	descriptor_heap& srv_heap();								// SRVヒープ
	descriptor_heap& uav_heap();								// UAVヒープ
	u32 current_frame_index();									// 現在のフレームインデックス
	void set_deferred_releases_flag();							// 遅延リリースフラグを設定

	surface create_surface(platform::window window);			// サーフェスを作成
	void remove_surface(surface_id id);							// サーフェスを削除
	void resize_surface(surface_id id, u32 width, u32 height);	// サーフェスのリサイズ
	u32 surface_width(surface_id id);							// サーフェスの幅
	u32 surface_height(surface_id id);							// サーフェスの高さ
	void render_surface(surface_id id);							// サーフェスのレンダリング
}	// namespace dxforge::graphics
