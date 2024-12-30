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

namespace dxforge::graphics::d3d12::core
{
	bool initialize(void);
	void shutdown(void);
	void render(void);

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

	ID3D12Device* const device();
	u32 current_frame_index();
	void set_deferred_release_flag();
}	// namespace dxforge::graphics
