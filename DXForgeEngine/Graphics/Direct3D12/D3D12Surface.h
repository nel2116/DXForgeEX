// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Surface.h]
// 作成日 : 2024/12/30
// 作成者 : 田中ミノル
// 概要
// 　Direct3D12サーフェス
// 更新履歴
// 2024/12/30 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12CommonHeaders.h"
#include "D3D12Resource.h"

namespace dxforge::graphics::d3d12
{
	class d3d12_surface
	{
	public:
		explicit d3d12_surface(platform::window window)
			: _window{ window }
		{
			assert(_window.handle());
		}

#if USE_STL_VECTOR
		DISABLE_COPY(d3d12_surface);

		constexpr d3d12_surface(d3d12_surface&& o)
			: _swap_chain{ o._swap_chain }, _window{ o._window }, _current_bb_index{ o._current_bb_index }
			, _viewport{ o._viewport }, _scissor_rect{ o._scissor_rect }, _allow_tearing{ o._allow_tearing }
			, _present_flags{ o._present_flags }
		{
			for (u32 i{ 0 }; i < frame_buffer_count; ++i)
			{
				_render_target_data[i].resource = o._render_target_data[i].resource;
				_render_target_data[i].rtv = o._render_target_data[i].rtv;
			}
			o.reset();
		}

		constexpr d3d12_surface& operator=(d3d12_surface&& o)
		{
			assert(this != &o);
			if (this != &o)
			{
				release();
				move(o);
			}
			return *this;
		}
#endif

		~d3d12_surface() { release(); }

		/// @brief スワップチェインの作成
		void create_swap_chain(IDXGIFactory7* factory, ID3D12CommandQueue* cmd_queue, DXGI_FORMAT format);

		/// @brief サーフェスの表示
		void present() const;

		/// @brief サーフェスのリサイズ
		void resize();

		// ------ アクセサ ------
		constexpr u32 width() const { return (u32)_viewport.Width; }
		constexpr u32 height() const { return (u32)_viewport.Height; }
		constexpr ID3D12Resource* const back_buffer() const { return _render_target_data[_current_bb_index].resource; }
		constexpr D3D12_CPU_DESCRIPTOR_HANDLE rtv() const { return _render_target_data[_current_bb_index].rtv.cpu; }
		constexpr const D3D12_VIEWPORT& viewport() const { return _viewport; }
		constexpr const D3D12_RECT& scissor_rect() const { return _scissor_rect; }

	private:

		void finalize();

		/// @brief リソースの解放
		void release();

#if USE_STL_VECTOR
		constexpr void move(d3d12_surface& o)
		{
			_swap_chain = o._swap_chain;
			for (u32 i{ 0 }; i < frame_buffer_count; ++i)
			{
				_render_target_data[i] = o._render_target_data[i];
			}
			_window = o._window;
			_current_bb_index = o._current_bb_index;
			_allow_tearing = o._allow_tearing;
			_present_flags = o._present_flags;
			_viewport = o._viewport;
			_scissor_rect = o._scissor_rect;
			o.reset();
		}
		constexpr void reset()
		{
			_swap_chain = nullptr;
			for (u32 i{ 0 }; i < frame_buffer_count; ++i)
			{
				_render_target_data[i] = {};
			}
			_window = {};
			_current_bb_index = 0;
			_allow_tearing = 0;
			_present_flags = 0;
			_viewport = {};
			_scissor_rect = {};
		}
#endif
		// ------ 構造体定義 ------
		struct render_target_data
		{
			ID3D12Resource* resource{ nullptr };						// リソース
			descriptor_handle rtv{};									// RTV
		};

		// ------ 変数 ------
		IDXGISwapChain4* _swap_chain{ nullptr };						// スワップチェイン
		render_target_data _render_target_data[frame_buffer_count]{};	// レンダーターゲットデータ
		platform::window _window{};										// ウィンドウ
		mutable u32 _current_bb_index{ 0 };								// 現在のバックバッファインデックス
		u32 _allow_tearing{ 0 };										// ティアリングを許可するかどうか
		u32 _present_flags{ 0 };										// プレゼントフラグ
		D3D12_VIEWPORT _viewport{};										// ビューポート
		D3D12_RECT _scissor_rect{};										// シザー矩形
	};
}
