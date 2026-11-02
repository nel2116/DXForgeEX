// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12Runtime.h]
// 作成日 : 2026/11/02
// 作成者 : 田中ミノル
// 概要
// 　使うD3D12のランタイム(Agility SDK か OS か)の選択と、それを通したデバイスの作成
// 更新履歴
// 2026/11/02 新規作成 (#155、Primal Day 079)
// 2026/11/02 D3D12GetInterface を GetProcAddress で解決する find_get_interface() を追加 (#155)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "D3D12CommonHeaders.h"
#include <string>

// NOTE: Primal Day 079 と同じく、ID3D12SDKConfiguration1::CreateDeviceFactory で、エンジンの exe/DLL の隣の D3D12\ にある
//		 Agility SDK のランタイム(D3D12Core.dll)からデバイスを作る。exe から D3D12SDKVersion/D3D12SDKPath を公開する方式と違い、
//		 .NET の exe(エディタ)でも、ゲームの exe・EngineTest・EngineUnitTests でも同じ経路になり、開発者モードも要らない。
//		 Agility SDK を使えないとき(D3D12\ がない、OS が ID3D12SDKConfiguration1 を持たないなど)は、警告を出して
//		 OS のランタイム(D3D12CreateDevice など)に戻る。
//		 この宣言は古い d3d12.h にもある型だけを使う(エンジン以外のプロジェクトは Windows SDK の d3d12.h でコンパイルするため)。
namespace dxforge::graphics::d3d12::runtime
{
	/// @brief デバイスを作る前に、Agility SDK のランタイムを使う準備をする。使えなければ OS のランタイムに戻る(失敗にはしない)
	/// @param unattended 無人実行のとき true。使ったランタイムを標準エラーにも出す
	void initialize(bool unattended);

	/// @brief デバイスを解放したあとで、Agility SDK のファクトリを解放する。info() の内容は次の initialize() まで残す
	void shutdown(void);

	/// @brief 選んだランタイムでデバイスを作る(D3D12CreateDevice と同じ引数。device が nullptr なら作れるかだけを確かめる)
	HRESULT create_device(IUnknown* adapter, D3D_FEATURE_LEVEL feature_level, REFIID riid, void** device);

	/// @brief 選んだランタイムのデバッグインターフェイスを取る(D3D12GetDebugInterface と同じ)
	HRESULT get_debug_interface(REFIID riid, void** debug);

	/// @brief デバイスのランタイムでルートシグネチャをシリアライズする(D3D12SerializeVersionedRootSignature と同じ)
	HRESULT serialize_root_signature(ID3D12Device* device, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc, ID3DBlob** blob, ID3DBlob** error_blob);

	/// @brief デバイスを作ったあとで、読み込まれた D3D12Core.dll(パスとバージョン)を調べ、デバッグ出力(無人実行では標準エラーにも)に出す
	void detect_loaded_runtime(void);

	/// @brief 使っているランタイムの情報(graphics::runtime())
	[[nodiscard]] runtime_info info(void);

	/// @brief d3d12.dll の D3D12GetInterface を探す。静的にインポートすると、それがない d3d12.dll(Agility SDK 対応前の Windows 10)では
	///		   exe/DLL の読み込みに失敗するため、GetProcAddress で解決する
	/// @param d3d12_module d3d12.dll のハンドル(nullptr なら読み込まれていない)
	/// @param fallback_reason 見つからないときに、OS のランタイムに戻る理由を入れる
	/// @return D3D12GetInterface(PFN_D3D12_GET_INTERFACE)。見つからなければ nullptr
	[[nodiscard]] FARPROC find_get_interface(HMODULE d3d12_module, std::string& fallback_reason);

	/// @brief エンジンのモジュール(exe か DLL)のパスから、Agility SDK のフォルダ(<モジュールのフォルダ>\D3D12\)を作る
	[[nodiscard]] std::wstring sdk_folder_for_module(const std::wstring& module_path);

	/// @brief VS_FIXEDFILEINFO のバージョン(dwFileVersionMS/LS)を "a.b.c.d" にする
	[[nodiscard]] std::string format_file_version(u32 version_ms, u32 version_ls);
}
