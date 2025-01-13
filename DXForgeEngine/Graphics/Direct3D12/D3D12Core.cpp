// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Core.cpp]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要
// 　Direct3D12のコア
// 更新履歴
// 2024/12/27 新規作成
// 2025/01/07 Gパスの追加
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "D3D12Core.h"
#include "D3D12Surface.h"
#include "D3D12Shaders.h"
#include "D3D12GPass.h"
#include "D3D12PostProcess.h"
#include "D3D12Upload.h"

using namespace Microsoft::WRL;	// ComPtrを使うため

// ====== 名前空間 ======
namespace dxforge::graphics::d3d12::core
{
	namespace
	{
		class d3d12_command
		{
		public:
			d3d12_command() = default;
			DISABLE_COPY_AND_MOVE(d3d12_command);
			explicit d3d12_command(id3d12_device* const device, D3D12_COMMAND_LIST_TYPE type)
			{
				HRESULT hr{ S_OK };

				// コマンドキューの設定
				D3D12_COMMAND_QUEUE_DESC desc{};
				desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
				desc.NodeMask = 0;
				desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
				desc.Type = type;

				// コマンドキューを作成
				DXCall(hr = device->CreateCommandQueue(&desc, IID_PPV_ARGS(&_cmd_queue)));

				// 失敗した場合はエラー処理に移動
				if (FAILED(hr)) goto _error;

				// コマンドキューに名前を付ける
				NAME_D3D12_OBJECT(_cmd_queue,
					type == D3D12_COMMAND_LIST_TYPE_DIRECT ?
					L"GFX Command Queue" :
					type == D3D12_COMMAND_LIST_TYPE_COMPUTE ?
					L"Compute Command Queue" : L"Command Queue");

				// コマンドアロケータを作成
				for (u32 i{ 0 }; i < frame_buffer_count; ++i)
				{
					command_frame& frame{ _cmd_frames[i] };
					DXCall(hr = device->CreateCommandAllocator(type, IID_PPV_ARGS(&frame.cmd_allocator)));
					if (FAILED(hr)) goto _error;
					NAME_D3D12_OBJECT_INDEXED(frame.cmd_allocator, i,
						type == D3D12_COMMAND_LIST_TYPE_DIRECT ?
						L"GFX Command Allocator" :
						type == D3D12_COMMAND_LIST_TYPE_COMPUTE ?
						L"Compute Command Allocator" : L"Command Allocator");
				}

				// コマンドリストを作成
				DXCall(hr = device->CreateCommandList(0, type, _cmd_frames[0].cmd_allocator, nullptr, IID_PPV_ARGS(&_cmd_list)));
				if (FAILED(hr)) goto _error;
				DXCall(hr = _cmd_list->Close());
				NAME_D3D12_OBJECT(_cmd_list,
					type == D3D12_COMMAND_LIST_TYPE_DIRECT ?
					L"GFX Command List" :
					type == D3D12_COMMAND_LIST_TYPE_COMPUTE ?
					L"Compute Command List" : L"Command List");

				// フェンスを作成
				DXCall(hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&_fence)));
				if (FAILED(hr)) goto _error;
				NAME_D3D12_OBJECT(_fence, L"D3D12 Fence");

				// フェンスイベントを作成
				_fence_event = CreateEventEx(nullptr, nullptr, 0, EVENT_ALL_ACCESS);
				assert(_fence_event);
				if (!_fence_event) goto _error;

				// 成功した場合はここで終了
				return;

			_error:
				release();
			}

			~d3d12_command()
			{
				assert(!_cmd_queue && !_cmd_list && !_fence);
			}

			/// @brief フレームの開始処理
			/// 現在のフレームがシグナルされるのを待ち、コマンドリスト／アロケータをリセットする
			void begin_frame()
			{
				//  ====== フレームの開始処理 ======
				command_frame& frame{ _cmd_frames[_frame_index] };
				// GPUがまだコマンドリストを実行中の場合は、フレームのコマンドアロケータをリセットする前に待機する
				frame.wait(_fence_event, _fence);
				// コマンドアロケータをリセット
				DXCall(frame.cmd_allocator->Reset());
				// コマンドリストをリセット
				DXCall(_cmd_list->Reset(frame.cmd_allocator, nullptr));
			}

			/// @brief フレームの終了処理
			/// 新しいフェンス値をフェンスに知らせる
			void end_frame(const d3d12_surface& surface)
			{
				//  ====== フレームの終了処理 ======
				// コマンドリストを閉じる
				DXCall(_cmd_list->Close());

				// コマンドリストを実行
				ID3D12CommandList* const cmd_lists[]{ _cmd_list };
				_cmd_queue->ExecuteCommandLists(_countof(cmd_lists), &cmd_lists[0]);

				// スワップチェーンバッファの提示は、フレームバッファと同期して行われる。
				surface.present();

				// フェンスにシグナルを送る
				u64& fence_value{ _fence_value };
				++fence_value;
				command_frame& frame{ _cmd_frames[_frame_index] };
				frame.fence_value = fence_value;
				_cmd_queue->Signal(_fence, fence_value);

				// フレームインデックスを更新
				_frame_index = (_frame_index + 1) % frame_buffer_count;
			}

			/// @brief フレームのフラッシュ
			void flush()
			{
				for (u32 i{ 0 }; i < frame_buffer_count; ++i)
				{
					_cmd_frames[i].wait(_fence_event, _fence);
				}
				_frame_index = 0;
			}

			void release()
			{
				// ====== リソースの解放 ======
				flush();
				core::release(_fence);		// フェンスの解放
				_fence_value = 0;			// フェンスの値をリセット

				CloseHandle(_fence_event);	// フェンスイベントの解放
				_fence_event = nullptr;

				core::release(_cmd_queue);	// コマンドキューの解放
				core::release(_cmd_list);	// コマンドリストの解放

				// コマンドアロケータの解放
				for (u32 i{ 0 }; i < frame_buffer_count; ++i)
				{
					_cmd_frames[i].release();
				}
			}

			/// @brief コマンドキューを取得
			/// @return ID3D12CommandQueue* コマンドキュー
			[[nodiscard]] constexpr ID3D12CommandQueue* const command_queue() const { return _cmd_queue; }
			/// @brief コマンドリストを取得
			/// @return ID3D12GraphicsCommandList6* コマンドリスト
			[[nodiscard]] constexpr id3d12_graphics_command_list* const command_list() const { return _cmd_list; }
			/// @brief フレームインデックスを取得
			/// @return u32 フレームインデックス
			[[nodiscard]] constexpr u32 frame_index() const { return _frame_index; }

		private:

			struct command_frame
			{
				ID3D12CommandAllocator* cmd_allocator{ nullptr };						// コマンドアロケータ
				u64 fence_value{ 0 };													// フェンスの値

				void wait(HANDLE fence_event, ID3D12Fence1* fence)
				{
					assert(fence && fence_event);
					// 現在のフェンス値がまだ "fence_value "より小さい場合
					// GPUは"_cmd_queue->Signal() "コマンドに到達していないため
					// コマンドリストの実行が終了していないことがわかる
					if (fence->GetCompletedValue() < fence_value)
					{
						// フェンスの現在値が "fence_value "と等しくなった時点で、フェンスがイベントを発生させる
						DXCall(fence->SetEventOnCompletion(fence_value, fence_event));
						// フェンスの現在値がコマンドキューの実行終了を示す「fence_value」に達したというイベントがトリガーされるまで待つ
						WaitForSingleObject(fence_event, INFINITE);
					}
				}

				void release()
				{
					core::release(cmd_allocator);
					fence_value = 0;
				}
			};

			ID3D12CommandQueue* _cmd_queue{ nullptr };									// コマンドキュー
			id3d12_graphics_command_list* _cmd_list{ nullptr };							// コマンドリスト
			ID3D12Fence1* _fence{ nullptr };											// フェンス
			u64 _fence_value{ 0 };														// フェンスの値
			HANDLE _fence_event{ nullptr };												// フェンスイベント
			command_frame _cmd_frames[frame_buffer_count];								// フレームバッファ
			u32 _frame_index{ 0 };														// フレームインデックス

		};	// class d3d12_command

		// ====== 変数 ======

		using surface_collection = utl::free_list<d3d12_surface>;							// サーフェスコレクション

		id3d12_device* main_device{ nullptr };											// メインデバイス
		IDXGIFactory7* dxgi_factory{ nullptr };											// DXGIファクトリ
		d3d12_command gfx_command;														// グラフィックスコマンド
		surface_collection surfaces{};													// サーフェス
		d3dx::d3d12_resource_barrier resource_barriers{};								// リソースバリア

		descriptor_heap rtv_desc_heap{ D3D12_DESCRIPTOR_HEAP_TYPE_RTV };				// RTVディスクリプタヒープ
		descriptor_heap dsv_desc_heap{ D3D12_DESCRIPTOR_HEAP_TYPE_DSV };				// DSVディスクリプタヒープ
		descriptor_heap srv_desc_heap{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV };		// SRVディスクリプタヒープ
		descriptor_heap uav_desc_heap{ D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV };		// UAVディスクリプタヒープ

		utl::vector<IUnknown*> deferred_releases[frame_buffer_count]{};					// 保留中のリソース
		u32 deferred_releases_flag[frame_buffer_count]{};								// 遅延解放フラグ
		std::mutex deferred_release_mutex{};											// 遅延解放ミューテックス

		constexpr D3D_FEATURE_LEVEL minimum_feature_level{ D3D_FEATURE_LEVEL_11_0 };	// 最低限必要な機能レベル

		// ====== 関数 ======
		/// @brief 初期化に失敗した場合の処理
		/// @return bool 初期化に失敗した場合はfalseを返す
		bool failed_init()
		{
			shutdown();
			return false;
		}

		/// @brief 最小限の機能レベルをサポートする、最も性能の高い最初のアダプターを入手する。
		/// @return IDXGIAdapter4* メインアダプター
		/// NOTE: この関数は、例えば、出力デバイス（スクリーンなど）が接続されているかどうかをチェックしたり、
		/// サポートされている解像度を列挙したり、マルチアダプター設定で使用するアダプターを選択する手段を提供するなど、
		/// 機能を拡張することができる。
		IDXGIAdapter4* determine_main_adapter(void)
		{
			IDXGIAdapter4* adapter{ nullptr };											// アダプター

			// 性能の高い順にアダプターを取得
			for (u32 i{ 0 }; dxgi_factory->EnumAdapterByGpuPreference(i, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i)
			{
				// 最小の機能レベルをサポートする最初のアダプタを選ぶ。
				if (SUCCEEDED(D3D12CreateDevice(adapter, minimum_feature_level, __uuidof(ID3D12Device), nullptr)))
				{
					return adapter;
				}
				release(adapter);
			}
			return nullptr;
		}

		/// @brief アダプターがサポートする最大の機能レベルを取得する
		/// @param adapter アダプター
		/// @return D3D_FEATURE_LEVEL 最大の機能レベル
		D3D_FEATURE_LEVEL get_max_feature_level(IDXGIAdapter4* adapter)
		{
			constexpr D3D_FEATURE_LEVEL feature_levels[4]
			{
				D3D_FEATURE_LEVEL_11_0,
				D3D_FEATURE_LEVEL_11_1,
				D3D_FEATURE_LEVEL_12_0,
				D3D_FEATURE_LEVEL_12_1,
			};

			D3D12_FEATURE_DATA_FEATURE_LEVELS feature_level_info{};
			feature_level_info.NumFeatureLevels = _countof(feature_levels);
			feature_level_info.pFeatureLevelsRequested = feature_levels;

			ComPtr<ID3D12Device> device;
			DXCall(D3D12CreateDevice(adapter, minimum_feature_level, IID_PPV_ARGS(&device)));
			DXCall(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &feature_level_info, sizeof(feature_level_info)));
			return feature_level_info.MaxSupportedFeatureLevel;
		}

		void __declspec(noinline) process_deferred_releases(u32 frame_idx)
		{
			std::lock_guard lock{ deferred_release_mutex };

			// NOTE: このフラグは最初にクリアする。
			// もし最後にこのフラグをクリアしたら、このフラグを設定しようとしていた他のスレッドを上書きしてしまうかもしれない。
			// 上書きはアイテムを処理する前に起こるので問題ない。
			deferred_releases_flag[frame_idx] = 0;

			rtv_desc_heap.process_deferred_free(frame_idx);
			dsv_desc_heap.process_deferred_free(frame_idx);
			srv_desc_heap.process_deferred_free(frame_idx);
			uav_desc_heap.process_deferred_free(frame_idx);

			utl::vector<IUnknown*>& resources{ deferred_releases[frame_idx] };
			if (!resources.empty())
			{
				for (auto& resource : resources) release(resource);
				resources.clear();
			}

		}

	}	// 匿名名前空間

	namespace detail
	{
		void deferred_release(IUnknown* ptr)
		{
			const u32 frame_idx{ current_frame_index() };
			std::lock_guard lock{ deferred_release_mutex };
			deferred_releases[frame_idx].push_back(ptr);
			set_deferred_releases_flag();
		}
	}	// namespace detail

	// ====== 関数 ======
	/// @brief Direct3D12の初期化の処理
	/// @return bool 初期化に成功した場合はtrueを返す
	bool initialize(void)
	{
		// サポーターの最大機能レベルを決める
		// ID3D12Device（仮想アダプタ）を作成する。

		// 既に初期化されている場合は先にあるものを破棄する
		if (main_device) shutdown();

		u32 dxgi_factory_flag{ 0 };
#if _DEBUG
		// デバッグレイヤーを有効にする。 グラフィックツールオプション機能が必要です。
		{
			ComPtr<ID3D12Debug3> debug_interface;
			if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface))))
			{
				debug_interface->EnableDebugLayer();
			}
			else
			{
				OutputDebugStringA("Warning: D3D12 デバッグインターフェイスは使用できません。 このデバイスにGraphics Toolsオプション機能がインストールされているか確認してください。\n");
			}
			dxgi_factory_flag |= DXGI_CREATE_FACTORY_DEBUG;
		}
#endif // _DEBUG

		HRESULT hr{ S_OK };
		// DXGIファクトリを作成
		DXCall(hr = CreateDXGIFactory2(dxgi_factory_flag, IID_PPV_ARGS(&dxgi_factory)));
		if (FAILED(hr)) return failed_init();

		// 使用するアダプター（グラフィックカード）を決定する。
		ComPtr<IDXGIAdapter4> main_adapter;
		main_adapter.Attach(determine_main_adapter());
		if (!main_adapter) return failed_init();

		D3D_FEATURE_LEVEL max_feature_level{ get_max_feature_level(main_adapter.Get()) };
		assert(max_feature_level >= minimum_feature_level);
		if (max_feature_level < minimum_feature_level) return failed_init();

		// デバイスを作成
		DXCall(D3D12CreateDevice(main_adapter.Get(), max_feature_level, IID_PPV_ARGS(&main_device)));
		if (!main_device) return failed_init();

		// デバイスのデバッグ情報を設定
#ifdef _DEBUG
		{
			ComPtr<ID3D12InfoQueue> info_queue;
			DXCall(main_device->QueryInterface(IID_PPV_ARGS(&info_queue)));
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		}
#endif // _DEBUG

		// ヒープを作成
		bool result{ true };
		result &= rtv_desc_heap.initialize(512, false);
		result &= dsv_desc_heap.initialize(512, false);
		result &= srv_desc_heap.initialize(4096, true);
		result &= uav_desc_heap.initialize(512, false);
		if (!result) return failed_init();

		// グラフィックスコマンドを作成
		new (&gfx_command) d3d12_command(main_device, D3D12_COMMAND_LIST_TYPE_DIRECT);
		if (!gfx_command.command_queue()) return failed_init();

		// モジュールの初期化
		if (!(shaders::initialize() && gpass::initialize() && fx::initialize() && upload::initialize())) return failed_init();

		// デバッグネームを設定
		NAME_D3D12_OBJECT(main_device, L"Main D3D12 Device");
		NAME_D3D12_OBJECT(rtv_desc_heap.heap(), L"RTV Descriptor Heap");
		NAME_D3D12_OBJECT(dsv_desc_heap.heap(), L"DSV Descriptor Heap");
		NAME_D3D12_OBJECT(srv_desc_heap.heap(), L"SRV Descriptor Heap");
		NAME_D3D12_OBJECT(uav_desc_heap.heap(), L"UAV Descriptor Heap");

		return true;
	}

	/// @brief Direct3D12の終了処理
	void shutdown(void)
	{
		// グラフィックスコマンドの解放
		gfx_command.release();

		// NOTE: 最後にprocess_deferred_releases()を呼ばないのは、いくつかのリソース（スワップチェーンなど）は、
		// 依存するリソースが解放される前に解放することができないからである。
		for (u32 i{ 0 }; i < frame_buffer_count; ++i)
		{
			process_deferred_releases(i);
		}

		// モジュールのシャットダウン
		upload::shutdown();
		fx::shutdown();
		gpass::shutdown();
		shaders::shutdown();

		// DXGIファクトリの解放
		release(dxgi_factory);

		// NOTE: 一部のモジュールは、シャットダウン時にディスクリプタを解放する。 process_deferred_free()をもう一度呼び出すことで、それらを処理する。
		rtv_desc_heap.process_deferred_free(0);
		dsv_desc_heap.process_deferred_free(0);
		srv_desc_heap.process_deferred_free(0);

		// ヒープの解放
		rtv_desc_heap.release();
		dsv_desc_heap.release();
		srv_desc_heap.release();
		uav_desc_heap.release();

		// NOTE: いくつかのタイプは、シャットダウン/リセット/クリア時にのみ、リソースのディファード・リリースを使用する。
		// これらのリソースを最終的に解放するために、process_deferred_releasesをもう一度呼び出す。
		process_deferred_releases(0);

		// デバッグモードでは、リークされたオブジェクトを報告する
#ifdef _DEBUG
		{
			{
				// デバッグレイヤーを無効にする
				ComPtr<ID3D12InfoQueue> info_queue;
				DXCall(main_device->QueryInterface(IID_PPV_ARGS(&info_queue)));
				info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false);
				info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false);
				info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
			}

			// デバッグデバイスを作成
			ComPtr<ID3D12DebugDevice> debug_device;
			DXCall(main_device->QueryInterface(IID_PPV_ARGS(&debug_device)));
			// メインデバイスを解放しておく
			release(main_device);
			// デバッグデバイスを使用して、リークされたオブジェクトを報告する(デバッグデバイスのみが残り、1つのオブジェクトがリークしているはず)
			DXCall(debug_device->ReportLiveDeviceObjects(D3D12_RLDO_SUMMARY | D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL));
		}
#endif // _DEBUG

		// メインデバイスの解放
		release(main_device);
	}

	id3d12_device* const device()
	{
		return main_device;
	}

	descriptor_heap& rtv_heap()
	{
		return rtv_desc_heap;
	}

	descriptor_heap& dsv_heap()
	{
		return dsv_desc_heap;
	}

	descriptor_heap& srv_heap()
	{
		return srv_desc_heap;
	}

	descriptor_heap& uav_heap()
	{
		return uav_desc_heap;
	}

	u32 current_frame_index()
	{
		return gfx_command.frame_index();
	}

	void set_deferred_releases_flag()
	{
		deferred_releases_flag[current_frame_index()] = 1;
	}

	surface create_surface(platform::window window)
	{
		surface_id id{ surfaces.add(window) };
		surfaces[id].create_swap_chain(dxgi_factory, gfx_command.command_queue());
		return surface{ id };
	}

	void remove_surface(surface_id id)
	{
		gfx_command.flush();
		surfaces.remove(id);
	}

	void resize_surface(surface_id id, u32/* width*/, u32/* height*/)
	{
		gfx_command.flush();
		surfaces[id].resize();
	}

	u32 surface_width(surface_id id)
	{
		return surfaces[id].width();
	}

	u32 surface_height(surface_id id)
	{
		return surfaces[id].height();
	}

	/// @brief レンダリング処理
	void render_surface(surface_id id)
	{
		// GPUがコマンド・アロケータを終了するのを待ち、GPUがコマンド・アロケータを終了したら、アロケータをリセットする。
		// これにより、コマンドの保存に使われていたメモリが解放される。
		gfx_command.begin_frame();
		id3d12_graphics_command_list* cmd_list{ gfx_command.command_list() };

		const u32 frame_idx{ current_frame_index() };
		if (deferred_releases_flag[frame_idx])
		{
			process_deferred_releases(frame_idx);
		}

		const d3d12_surface& surface{ surfaces[id] };
		ID3D12Resource* const current_back_buffer{ surface.back_buffer() };

		d3d12_frame_info frame_info
		{
			surface.width(),
			surface.height()
		};

		gpass::set_size({ frame_info.surface_width, frame_info.surface_height });
		d3dx::d3d12_resource_barrier& barriers{ resource_barriers };

		// コマンドの記録
		ID3D12DescriptorHeap* const heaps[]{ srv_desc_heap.heap() };
		cmd_list->SetDescriptorHeaps(1, &heaps[0]);

		cmd_list->RSSetViewports(1, &surface.viewport());
		cmd_list->RSSetScissorRects(1, &surface.scissor_rect());

		// Depth prepass
		barriers.add(current_back_buffer,
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_BARRIER_FLAG_BEGIN_ONLY);
		gpass::add_transitions_for_depth_prepass(barriers);
		barriers.apply(cmd_list);
		gpass::set_render_targets_for_depth_prepass(cmd_list);
		gpass::depth_prepass(cmd_list, frame_info);

		// Geometry and lighting pass
		gpass::add_transitions_for_gpass(barriers);
		barriers.apply(cmd_list);
		gpass::set_render_targets_for_gpass(cmd_list);
		gpass::render(cmd_list, frame_info);

		// Post-process
		barriers.add(current_back_buffer,
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_BARRIER_FLAG_END_ONLY);
		gpass::add_transitions_for_post_process(barriers);
		barriers.apply(cmd_list);
		// 現在のバックバッファに書き込むので、バックバッファはレンダリングターゲットになる
		fx::post_process(cmd_list, surface.rtv());
		// after post process
		d3dx::transition_resource(cmd_list, current_back_buffer,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT);

		// コマンドの録音が終わった。 コマンドを実行してください、
		// 信号を受信し、次のフレームのフェンス値をインクリメントする。
		gfx_command.end_frame(surface);
	}

}	// namespace dxforge::graphics