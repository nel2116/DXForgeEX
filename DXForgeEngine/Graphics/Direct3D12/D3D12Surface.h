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

namespace dxforge::graphics::d3d12
{
	class d3d12_surface
	{
	public:	// 定数
		constexpr static DXGI_FORMAT default_back_buffer_format{ DXGI_FORMAT_R16G16B16A16_FLOAT };	// デフォルトのバックバッファフォーマット
		constexpr static u32 buffer_count{ 3 };	// バッファ数

	public:	// メソッド
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
			, _present_flags{ o._present_flags }, _light_culling_id{ o._light_culling_id }
		{
			for (u32 i{ 0 }; i < buffer_count; ++i)
			{
				_render_target_data[i] = o._render_target_data[i];
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
#else
		DISABLE_COPY_AND_MOVE(d3d12_surface);
#endif	// USE_STL_VECTOR

		~d3d12_surface() { release(); }

		/// @brief スワップチェインの作成
		void create_swap_chain(IDXGIFactory7* factory, ID3D12CommandQueue* cmd_queue);

		/// @brief サーフェスの表示
		void present() const;

		/// @brief サーフェスのリサイズ
		void resize();

		// ------ アクセサ ------
		[[nodiscard]] constexpr u32 width() const { return (u32)_viewport.Width; }
		[[nodiscard]] constexpr u32 height() const { return (u32)_viewport.Height; }
		[[nodiscard]] constexpr ID3D12Resource* const back_buffer() const { return _render_target_data[_current_bb_index].resource; }
		[[nodiscard]] constexpr D3D12_CPU_DESCRIPTOR_HANDLE rtv() const { return _render_target_data[_current_bb_index].rtv.cpu; }
		[[nodiscard]] constexpr const D3D12_VIEWPORT& viewport() const { return _viewport; }
		[[nodiscard]] constexpr const D3D12_RECT& scissor_rect() const { return _scissor_rect; }
		[[nodiscard]] constexpr id::id_type light_culling_id() const { return _light_culling_id; }

	private:

		void finalize();

		/// @brief リソースの解放
		void release();

#if USE_STL_VECTOR
		constexpr void move(d3d12_surface& o)
		{
			_swap_chain = o._swap_chain;
			for (u32 i{ 0 }; i < buffer_count; ++i)
			{
				_render_target_data[i] = o._render_target_data[i];
			}
			_window = o._window;
			_current_bb_index = o._current_bb_index;
			_allow_tearing = o._allow_tearing;
			_present_flags = o._present_flags;
			_viewport = o._viewport;
			_scissor_rect = o._scissor_rect;
			_light_culling_id = o._light_culling_id;
			o.reset();
		}
		constexpr void reset()
		{
			_swap_chain = nullptr;
			for (u32 i{ 0 }; i < buffer_count; ++i)
			{
				_render_target_data[i] = {};
			}
			_window = {};
			_current_bb_index = 0;
			_allow_tearing = 0;
			_present_flags = 0;
			_viewport = {};
			_scissor_rect = {};
			_light_culling_id = id::invalid_id;
		}
#endif	// USE_STL_VECTOR

		// ------ 構造体定義 ------
		struct render_target_data
		{
			ID3D12Resource* resource{ nullptr };						// リソース
			descriptor_handle rtv{};									// RTV
		};

		// ------ 変数 ------
			// NOTE: ここに新しいメンバー・データを追加する場合は、移動コンストラクタを更新し、移動()関数とリセット()関数を更新することを忘れないでください。
			//		これは、（STLから）std::vectorを使用する際に正しい動作をさせるためです。
		IDXGISwapChain4* _swap_chain{ nullptr };						// スワップチェイン
		render_target_data _render_target_data[buffer_count]{};			// レンダーターゲットデータ
		platform::window _window{};										// ウィンドウ
		mutable u32 _current_bb_index{ 0 };								// 現在のバックバッファインデックス
		u32 _allow_tearing{ 0 };										// ティアリングを許可するかどうか
		u32 _present_flags{ 0 };										// プレゼントフラグ
		D3D12_VIEWPORT _viewport{};										// ビューポート
		D3D12_RECT _scissor_rect{};										// シザー矩形
		id::id_type _light_culling_id{ id::invalid_id };				// ライトカリングID
	};
}
