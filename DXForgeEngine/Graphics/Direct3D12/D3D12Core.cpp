// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Core.cpp]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要
// 　Direct3D12のコア
// 更新履歴
// 2024/12/27 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "D3D12Core.h"

using namespace Microsoft::WRL;	// ComPtrを使うため

// ====== 名前空間 ======
namespace dxforge::graphics::d3d12::core
{
	namespace
	{
		ID3D12Device* main_device{ nullptr };											// メインデバイス
		IDXGIFactory7* dxgi_factory{ nullptr };											// DXGIファクトリ

		constexpr D3D_FEATURE_LEVEL minimum_feature_level{ D3D_FEATURE_LEVEL_11_0 };	// 最低限必要な機能レベル

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
			IDXGIAdapter4* adapter{ nullptr };

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

	}	// 匿名名前空間

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
			DXCall(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface)));
			debug_interface->EnableDebugLayer();
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

		NAME_D3D12_OBJECT(main_device, L"Main D3D12 Device");

#ifdef _DEBUG
		{
			ComPtr<ID3D12InfoQueue> info_queue;
			DXCall(main_device->QueryInterface(IID_PPV_ARGS(&info_queue)));
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, true);
			info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, true);
		}
#endif // _DEBUG


		return true;
	}


	void shutdown(void)
	{
		release(dxgi_factory);
#ifdef _DEBUG
		{
			{
				ComPtr<ID3D12InfoQueue> info_queue;
				DXCall(main_device->QueryInterface(IID_PPV_ARGS(&info_queue)));
				info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, false);
				info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, false);
				info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, false);
			}

			ComPtr<ID3D12DebugDevice> debug_device;
			DXCall(main_device->QueryInterface(IID_PPV_ARGS(&debug_device)));
			release(main_device);
			DXCall(debug_device->ReportLiveDeviceObjects(D3D12_RLDO_SUMMARY | D3D12_RLDO_DETAIL | D3D12_RLDO_IGNORE_INTERNAL));
		}
#endif // _DEBUG
		release(main_device);
	}
}	// namespace dxforge::graphics