// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Window.h]
// 作成日 : 2024/12/20
// 作成者 : 田中ミノル
// 概要 :
// 　ウィンドウ
// 更新履歴
// 2024/12/20 新規作成
// // _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"

namespace dxforge::platform
{
	DEFINE_TYPED_ID(window_id);

	class window
	{
	public:
		constexpr explicit window(window_id id) : _id{ id } {}
		constexpr window() = default;
		constexpr window_id get_id() const { return _id; }
		constexpr bool is_valid() const { return id::is_valid(_id); }

		void set_fullscrean(bool is_fullscreen) const;
		bool is_fullscreen() const;
		void* handle() const;
		void set_caption(const wchar_t* caption) const;
		math::u32v4 size() const;
		void resize(u32 width, u32 height) const;
		u32 width() const;
		u32 height() const;
		bool is_closed() const;

	private:
		window_id _id{ id::invalid_id };
	};
}



