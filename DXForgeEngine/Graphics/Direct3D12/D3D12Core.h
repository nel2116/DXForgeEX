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
	class descriptor_heap;
}

namespace dxforge::graphics::d3d12::core
{
	bool initialize(void);
	void shutdown(void);

	// 安全にリソースを解放する
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
	ID3D12Device8* const device();
	descriptor_heap& rtv_heap();
	descriptor_heap& dsv_heap();
	descriptor_heap& srv_heap();
	descriptor_heap& uav_heap();
	DXGI_FORMAT default_render_target_format();
	u32 current_frame_index();
	void set_deferred_releases_flag();

	surface create_surface(platform::window window);
	void remove_surface(surface_id id);
	void resize_surface(surface_id id, u32 width, u32 height);
	u32 surface_width(surface_id id);
	u32 surface_height(surface_id id);
	void render_surface(surface_id id);
}	// namespace dxforge::graphics
