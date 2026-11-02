// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Runtime.cpp]
// 作成日 : 2026/11/02
// 作成者 : 田中ミノル
// 概要
// 　使うD3D12のランタイム(Agility SDK か OS か)の選択と、それを通したデバイスの作成
// 更新履歴
// 2026/11/02 新規作成 (#155、Primal Day 079)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
// NOTE: CLSID_D3D12SDKConfiguration/CLSID_D3D12Debug の実体をこの翻訳単位で作る(Primal Day 079 の D3D12Core.cpp と同じ)。
//		 DEFINE_GUID は C++ では selectany なので、ほかで定義されていても重ならない。
#include <initguid.h>
#include "D3D12Runtime.h"
#include <cstdio>
#include <psapi.h>
#include <string>

#pragma comment(lib, "version.lib")

using namespace Microsoft::WRL;

// ====== 名前空間 ======
namespace dxforge::graphics::d3d12::runtime
{
	namespace
	{
		// ====== 変数 ======
		// NOTE: バージョンは Agility SDK(DXForgeEngine/packages.config の Microsoft.Direct3D.D3D12)の d3d12.h の値を使い、
		//		 ヘッダとランタイムのバージョンを二重に持たない。
		constexpr u32 agility_sdk_version{ D3D12_SDK_VERSION };

		ID3D12SDKConfiguration1* sdk_config{ nullptr };			// Agility SDK の設定(使えなければnullptr)
		ID3D12DeviceFactory* device_factory{ nullptr };			// Agility SDK のデバイスファクトリ(nullptrならOSのランタイム)
		bool unattended_mode{ false };							// 無人実行(標準エラーにも出す)

		// info() が返す文字列の実体(UTF-8)。次の initialize() まで残す
		std::string core_version{};
		std::string core_path{};
		std::string fallback_reason{};
		bool agility_sdk_used{ false };
		bool initialized_once{ false };

		// ====== 関数 ======
		std::string to_utf8(const std::wstring& text)
		{
			if (text.empty()) return {};
			const int size{ WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr) };
			std::string result(size > 0 ? (size_t)size : 0, '\0');
			if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), result.data(), size, nullptr, nullptr);
			return result;
		}

		/// @brief モジュールのフルパス(長いパスでも切らない)
		std::wstring module_path(HMODULE module)
		{
			std::wstring path(MAX_PATH, L'\0');
			for (;;)
			{
				const DWORD length{ GetModuleFileNameW(module, path.data(), (DWORD)path.size()) };
				if (length == 0) return {};
				if (length < path.size())
				{
					path.resize(length);
					return path;
				}
				path.resize(path.size() * 2);
			}
		}

		/// @brief このコードを含むモジュール(エディタでは DXForgeEngineDLL.dll、それ以外は exe)のパス
		std::wstring engine_module_path()
		{
			HMODULE module{ nullptr };
			if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&engine_module_path), &module))
			{
				return {};
			}
			return module_path(module);
		}

		/// @brief ファイルのバージョン(バージョンリソース)。読めなければ空
		std::string file_version(const std::wstring& path)
		{
			DWORD handle{ 0 };
			const DWORD size{ GetFileVersionInfoSizeW(path.c_str(), &handle) };
			if (!size) return {};
			std::string buffer(size, '\0');
			if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data())) return {};
			VS_FIXEDFILEINFO* info{ nullptr };
			UINT length{ 0 };
			if (!VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&info), &length) || !info || length < sizeof(VS_FIXEDFILEINFO)) return {};
			return format_file_version(info->dwFileVersionMS, info->dwFileVersionLS);
		}

		/// @brief 失敗の理由の文字列("<何をして> failed (0x........)")
		std::string failure(const char* what, HRESULT hr)
		{
			char text[160]{};
			std::snprintf(text, sizeof(text), "%s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
			return text;
		}

		void release_factory()
		{
			if (device_factory) { device_factory->Release(); device_factory = nullptr; }
			if (sdk_config) { sdk_config->Release(); sdk_config = nullptr; }
		}

		/// @brief デバッグ出力と(無人実行では)標準エラーに 1 行出す。text はUTF-8
		void log(const std::string& text)
		{
			const int size{ MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0) };
			std::wstring wide(size > 0 ? (size_t)size : 0, L'\0');
			if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), size);
			OutputDebugStringW(wide.c_str());
			OutputDebugStringW(L"\n");
			if (unattended_mode) std::fprintf(stderr, "%s\n", text.c_str());
		}
	}	// 匿名名前空間

	std::wstring sdk_folder_for_module(const std::wstring& module_path)
	{
		const size_t separator{ module_path.find_last_of(L"\\/") };
		if (separator == std::wstring::npos) return L".\\D3D12\\";
		return module_path.substr(0, separator + 1) + L"D3D12\\";
	}

	std::string format_file_version(u32 version_ms, u32 version_ls)
	{
		char text[64]{};
		std::snprintf(text, sizeof(text), "%u.%u.%u.%u", (u32)HIWORD(version_ms), (u32)LOWORD(version_ms), (u32)HIWORD(version_ls), (u32)LOWORD(version_ls));
		return text;
	}

	void initialize(bool unattended)
	{
		release_factory();
		unattended_mode = unattended;
		agility_sdk_used = false;
		core_version.clear();
		core_path.clear();
		fallback_reason.clear();
		initialized_once = true;

		HRESULT hr{ D3D12GetInterface(CLSID_D3D12SDKConfiguration, IID_PPV_ARGS(&sdk_config)) };
		if (FAILED(hr))
		{
			// OS の d3d12.dll が ID3D12SDKConfiguration1 を持たない(古い Windows)
			fallback_reason = failure("D3D12GetInterface(ID3D12SDKConfiguration1)", hr);
			release_factory();
			return;
		}

		// NOTE: CreateDeviceFactory のパスは UTF-8 で渡す(ANSI で表せない文字のフォルダでも読める。このマシンで確かめた、#155)。
		//		 相対パスは、作業フォルダではなく exe のフォルダからになる。エディタでは DLL が exe と別のフォルダにあってもよいように、
		//		 エンジンのモジュールのフォルダからの絶対パスにする。
		const std::string folder{ to_utf8(sdk_folder_for_module(engine_module_path())) };
		hr = sdk_config->CreateDeviceFactory(agility_sdk_version, folder.c_str(), IID_PPV_ARGS(&device_factory));
		if (FAILED(hr))
		{
			// D3D12\D3D12Core.dll がない、バージョンが合わないなど(このマシンでは 0x887E0003 が返った)
			fallback_reason = failure("ID3D12SDKConfiguration1::CreateDeviceFactory", hr) + " for " + folder;
			release_factory();
			return;
		}
		agility_sdk_used = true;
	}

	void shutdown(void)
	{
		release_factory();
	}

	HRESULT create_device(IUnknown* adapter, D3D_FEATURE_LEVEL feature_level, REFIID riid, void** device)
	{
		if (device_factory) return device_factory->CreateDevice(adapter, feature_level, riid, device);
		return D3D12CreateDevice(adapter, feature_level, riid, device);
	}

	HRESULT get_debug_interface(REFIID riid, void** debug)
	{
		if (device_factory) return device_factory->GetConfigurationInterface(CLSID_D3D12Debug, riid, debug);
		return D3D12GetDebugInterface(riid, debug);
	}

	HRESULT serialize_root_signature(ID3D12Device* device, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc, ID3DBlob** blob, ID3DBlob** error_blob)
	{
		// Agility SDK のデバイスは、そのランタイムでシリアライズする(Primal Day 079 の create_root_signature())
		if (device_factory && device)
		{
			ComPtr<ID3D12DeviceConfiguration1> device_config;
			const HRESULT hr{ device->QueryInterface(IID_PPV_ARGS(&device_config)) };
			if (FAILED(hr)) return hr;
			return device_config->SerializeVersionedRootSignature(desc, blob, error_blob);
		}
		return D3D12SerializeVersionedRootSignature(desc, blob, error_blob);
	}

	void detect_loaded_runtime(void)
	{
		// 読み込まれた D3D12Core.dll のうち、Agility SDK を使っているならそのフォルダのもの、OS のランタイムならそれ以外のもの
		const std::wstring sdk_folder{ sdk_folder_for_module(engine_module_path()) };
		HMODULE modules[1024]{};
		DWORD needed{ 0 };
		std::wstring found{};
		if (K32EnumProcessModules(GetCurrentProcess(), modules, sizeof(modules), &needed))
		{
			const DWORD listed{ needed / (DWORD)sizeof(HMODULE) };
			const DWORD count{ listed < (DWORD)_countof(modules) ? listed : (DWORD)_countof(modules) };
			for (DWORD i{ 0 }; i < count; ++i)
			{
				const std::wstring path{ module_path(modules[i]) };
				const size_t separator{ path.find_last_of(L"\\/") };
				const std::wstring name{ separator == std::wstring::npos ? path : path.substr(separator + 1) };
				if (_wcsicmp(name.c_str(), L"D3D12Core.dll") != 0) continue;
				const bool in_sdk_folder{ path.size() > sdk_folder.size() && _wcsnicmp(path.c_str(), sdk_folder.c_str(), sdk_folder.size()) == 0 };
				if (in_sdk_folder == agility_sdk_used)
				{
					found = path;
					break;
				}
			}
		}
		core_path = to_utf8(found);
		core_version = found.empty() ? std::string{} : file_version(found);

		char text[96]{};
		if (agility_sdk_used)
		{
			std::snprintf(text, sizeof(text), "[D3D12] runtime: Agility SDK %u", agility_sdk_version);
			log(std::string{ text } + " (D3D12Core " + (core_version.empty() ? "unknown" : core_version) + ", " + core_path + ")");
		}
		else
		{
			std::snprintf(text, sizeof(text), "[D3D12] warning: the Agility SDK %u is not used: ", agility_sdk_version);
			log(std::string{ text } + fallback_reason);
			log("[D3D12] runtime: OS (D3D12Core " + (core_version.empty() ? std::string{ "unknown" } : core_version) + ", " + core_path + ")");
		}
	}

	runtime_info info(void)
	{
		runtime_info result{};
		if (!initialized_once) return result;
		result.agility_sdk = agility_sdk_used;
		result.sdk_version = agility_sdk_version;
		result.core_version = core_version.c_str();
		result.core_path = core_path.c_str();
		result.fallback_reason = fallback_reason.c_str();
		return result;
	}
}
