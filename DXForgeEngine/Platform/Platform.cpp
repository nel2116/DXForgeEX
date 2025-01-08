// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Platform.cpp]
// 作成日 : 2024/12/20
// 作成者 : 田中ミノル
// 概要 :
// 　プラットフォームの実装
// 更新履歴
// 2024/12/20 新規作成
// // _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Platform.h"
#include "PlatformType.h"

namespace dxforge::platform
{

#ifdef  _WIN64

	namespace
	{
		/// @brief ウィンドウ情報
		/// @details ウィンドウの情報を保持する構造体
		struct window_info
		{
			HWND hwnd{ nullptr };
			RECT cliant_area{ 0,0,1920,1080 };
			RECT fullscreen_area{};
			POINT top_left{ 0,0 };
			DWORD style{ WS_VISIBLE };
			bool is_fullscreen{ false };
			bool is_closed{ false };
			~window_info() { assert(!is_fullscreen); }
		};

		// ウィンドウ情報のリスト
		utl::free_list<window_info> windows;

		/// @brief ウィンドウ情報の取得
		/// @param id ウィンドウID
		/// @return window_infoの参照
		window_info& get_from_id(window_id id)
		{
			assert(windows[id].hwnd);
			return windows[id];
		}

		/// @brief ウィンドウ情報の取得
		/// @param handle ウィンドウハンドル
		/// @return window_infoの参照
		window_info& get_from_handle(window_handle handle)
		{
			const window_id id{ (id::id_type)GetWindowLongPtr(handle, GWLP_USERDATA) };
			return get_from_id(id);
		}

		bool resized{ false };

		/// @brief ウィンドウプロシージャ
		/// @param hwnd
		/// @param msg
		/// @param wparam
		/// @param lparam
		/// @return LRESULT
		LRESULT CALLBACK internal_window_proc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam)
		{
			switch (msg)
			{
			case WM_NCCREATE:
			{
				// ウィンドウのデータ・バッファのユーザー・データ・フィールドにウィンドウIDを入れる。
				DEBUG_OP(SetLastError(0));
				const window_id id{ windows.add() };
				windows[id].hwnd = hwnd;
				SetWindowLongPtr(hwnd, GWLP_USERDATA, ((LONG_PTR)id));
				assert(GetLastError() == 0);
			}
			break;
			case WM_DESTROY:
				get_from_handle(hwnd).is_closed = true;
				break;

			case WM_SIZE:
				resized = (wparam != SIZE_MINIMIZED);
				break;

			default:
				break;
			}

			if (resized && GetAsyncKeyState(VK_LBUTTON) >= 0)
			{
				window_info& info{ get_from_handle(hwnd) };
				assert(info.hwnd);
				GetClientRect(info.hwnd, info.is_fullscreen ? &info.fullscreen_area : &info.cliant_area);
				resized = false;
			}

			LONG_PTR long_ptr{ GetWindowLongPtr(hwnd,0) };
			return long_ptr
				? ((window_proc)long_ptr)(hwnd, msg, wparam, lparam)
				: DefWindowProc(hwnd, msg, wparam, lparam);
		}


		/// @brief ウィンドウのリサイズ
		/// @param info ウィンドウ情報
		/// @param rect ウィンドウのサイズ
		void resize_window(window_info& info, const RECT& area)
		{
			// デバイスサイズに合わせてウィンドウサイズを調整する
			RECT window_rect{ area };
			AdjustWindowRect(&window_rect, info.style, FALSE);

			const s32 width{ window_rect.right - window_rect.left };
			const s32 height{ window_rect.bottom - window_rect.top };

			MoveWindow(info.hwnd, info.top_left.x, info.top_left.y, width, height, true);
		}

		/// @brief ウィンドウのリサイズ
		/// @param id ウィンドウID
		/// @param width ウィンドウの幅
		/// @param height ウィンドウの高さ
		void resize_window(window_id id, u32 width, u32 height)
		{
			window_info& info{ get_from_id(id) };

			// NOTE: レベルエディタでウィンドウをホストするとき、内部データ (クライアント領域の寸法) を更新します。
			if (info.style & WS_CHILD)
			{
				// 子ウィンドウの場合は、親ウィンドウのクライアント領域を取得する
				GetClientRect(info.hwnd, &info.cliant_area);
			}
			else
			{
				// NOTE: 画面解像度が変更された場合にも対応できるよう、フルスクリーンモードでのリサイズも行っています。
				RECT& area{ info.is_fullscreen ? info.fullscreen_area : info.cliant_area };
				area.bottom = area.top + height;
				area.right = area.left + width;
				resize_window(info, area);
			}
		}

		/// @brief ウィンドウのリサイズ
		/// @param id ウィンドウID
		/// @param is_fullscreen フルスクリーンにするかどうか
		void set_window_fullscreen(window_id id, bool is_fullscreen)
		{
			// ウィンドウ情報の取得
			window_info& info{ get_from_id(id) };

			// フルスクリーン状態が変更されたときの処理
			if (info.is_fullscreen != is_fullscreen)
			{
				// フルスクリーン状態の設定
				info.is_fullscreen = is_fullscreen;

				if (is_fullscreen)
				{	// フルスクリーン状態にする
					// 現在のウィンドウのサイズを保存し、フルスクリーン状態から切り替えたときに復元できるようにする。
					GetWindowRect(info.hwnd, &info.cliant_area);
					RECT rect;
					GetWindowRect(info.hwnd, &rect);
					info.top_left.x = rect.left;
					info.top_left.y = rect.top;
					SetWindowLongPtr(info.hwnd, GWL_STYLE, 0);
					ShowWindow(info.hwnd, SW_MAXIMIZE);
				}
				else
				{	// ウィンドウ状態にする
					SetWindowLongPtr(info.hwnd, GWL_STYLE, info.style);
					resize_window(info, info.cliant_area);
					ShowWindow(info.hwnd, SW_SHOWNORMAL);
				}

			}
		}

		bool is_window_fullscreen(window_id id)
		{
			return get_from_id(id).is_fullscreen;
		}

		window_handle get_window_handle(window_id id)
		{
			return get_from_id(id).hwnd;
		}

		void set_window_caption(window_id id, const wchar_t* caption)
		{
			window_info& info{ get_from_id(id) };
			SetWindowText(info.hwnd, caption);
		}

		math::u32v4 get_window_size(window_id id)
		{
			window_info& info{ get_from_id(id) };
			RECT& area{ info.is_fullscreen ? info.fullscreen_area : info.cliant_area };
			return { (u32)area.left,(u32)area.top, (u32)area.right, (u32)area.bottom };
		}

		bool is_window_closed(window_id id)
		{
			return get_from_id(id).is_closed;
		}

	}	// 匿名名前空間

	/// @brief ウィンドウの作成
	/// @param init_info ウィンドウの初期化情報
	/// @return window
	window create_window(const window_init_info* const init_info /* = nullptr */)
	{
		window_proc callback{ init_info ? init_info->callback : nullptr };
		window_handle parent{ init_info ? init_info->parent : nullptr };

		// ウィンドウクラスの設定
		WNDCLASSEX wc{};
		ZeroMemory(&wc, sizeof(wc));
		wc.cbSize = sizeof(WNDCLASSEX);
		wc.style = CS_HREDRAW | CS_VREDRAW;
		wc.lpfnWndProc = internal_window_proc;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = callback ? sizeof(callback) : 0;
		wc.hInstance = 0;
		wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
		wc.hCursor = LoadCursor(NULL, IDC_ARROW);
		wc.hbrBackground = CreateSolidBrush(RGB(26, 48, 76));
		wc.lpszMenuName = NULL;
		wc.lpszClassName = L"DXForgeWindow";
		wc.hIconSm = LoadIcon(NULL, IDI_APPLICATION);

		// ウィンドウクラスの登録
		RegisterClassEx(&wc);

		window_info info{};
		info.cliant_area.right = (init_info && init_info->width) ? info.cliant_area.left + init_info->width : info.cliant_area.right;
		info.cliant_area.bottom = (init_info && init_info->height) ? info.cliant_area.top + init_info->height : info.cliant_area.bottom;
		info.style |= parent ? WS_CHILD : WS_OVERLAPPEDWINDOW;

		RECT rect{ info.cliant_area };

		// デバイスのサイズに合わせてウィンドウサイズを調整
		AdjustWindowRect(&rect, info.style, FALSE);

		const wchar_t* caption{ (init_info && init_info->caption) ? init_info->caption : L"DXForge Game" };
		const s32 left{ init_info ? init_info->left : info.top_left.x };
		const s32 top{ init_info ? init_info->top : info.top_left.y };
		const s32 width{ rect.right - rect.left };
		const s32 height{ rect.bottom - rect.top };

		// ウィンドウクラスのインスタンスを作成する
		info.hwnd = CreateWindowEx(
			0,					// エクステンドスタイル
			wc.lpszClassName,	// クラス名
			caption,			// ウィンドウ名
			info.style,			// スタイル
			left,				// ウィンドウの初期位置 X
			top,				// ウィンドウの初期位置 Y
			width,				// ウィンドウの初期幅
			height,				// ウィンドウの初期高さ
			parent,				// 親ウィンドウのハンドル
			NULL,				// メニューハンドル
			NULL,				// このアプリケーションのインスタンス
			NULL);				// その他のパラメータ

		if (info.hwnd)
		{
			// ウィンドウのメッセージを処理するウィンドウコールバック関数へのポインタを "extra "バイトにセットする。
			DEBUG_OP(SetLastError(0));
			if (callback) SetWindowLongPtr(info.hwnd, 0, (LONG_PTR)callback);
			assert(GetLastError() == 0);
			ShowWindow(info.hwnd, SW_SHOWNORMAL);
			UpdateWindow(info.hwnd);

			window_id id{ (id::id_type)GetWindowLongPtr(info.hwnd,GWLP_USERDATA) };
			windows[id] = info;

			return window{ id };
		}
		return {};
	}

	void remove_window(window_id id)
	{
		window_info& info{ get_from_id(id) };
		DestroyWindow(info.hwnd);
		windows.remove(id);
	}
#else
#error "must implementat least one platform"
#endif // ! _WIN64

	void window::set_fullscrean(bool is_fullscreen) const
	{
		assert(is_valid());
		set_window_fullscreen(_id, is_fullscreen);
	}

	bool window::is_fullscreen() const
	{
		assert(is_valid());
		return is_window_fullscreen(_id);
	}

	void* window::handle() const
	{
		assert(is_valid());
		return get_window_handle(_id);
	}

	void window::set_caption(const wchar_t* caption) const
	{
		assert(is_valid());
		set_window_caption(_id, caption);
	}

	math::u32v4 window::size() const
	{
		assert(is_valid());
		return get_window_size(_id);
	}

	void window::resize(u32 width, u32 height) const
	{
		assert(is_valid());
		resize_window(_id, width, height);
	}

	u32 window::width() const
	{
		math::u32v4 s{ size() };
		return s.z - s.x;
	}

	u32 window::height() const
	{
		math::u32v4 s{ size() };
		return s.w - s.y;
	}

	bool window::is_closed() const
	{
		assert(is_valid());
		return is_window_closed(_id);
	}
}

