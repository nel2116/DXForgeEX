// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_D3D12Runtime.cpp]
// 作成日 : 2026/11/02
// 作成者 : 田中ミノル
// 概要 :
// D3D12Runtime(D3D12 Agility SDK のランタイムの選択)のうち、GPUの要らない部分のテスト。
// 実際に Agility SDK で初期化できること、D3D12\D3D12Core.dll がないときに OS のランタイムに戻ることは、
// --render のヘッドレス実行(EngineUnitTests/Tests/render_run_test.py)で確かめる。
// 更新履歴
// 2026/11/02 新規作成 (#155)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"
#include "Graphics/Direct3D12/D3D12Runtime.h"

using namespace dxforge::graphics::d3d12;

DXFORGE_TEST_CASE(D3D12Runtime_SdkFolderIsNextToTheEngineModule)
{
	// エディタでは DXForgeEngineDLL.dll、ゲームや EngineTest では exe の隣の D3D12\ を使う
	CHECK(runtime::sdk_folder_for_module(L"C:\\Games\\My Game\\Game.exe") == L"C:\\Games\\My Game\\D3D12\\");
	CHECK(runtime::sdk_folder_for_module(L"D:\\DXForgeEX\\x64\\DebugEditor\\DXForgeEngineDLL.dll") == L"D:\\DXForgeEX\\x64\\DebugEditor\\D3D12\\");
	// ASCII でない文字のフォルダもそのまま(CreateDeviceFactory には UTF-8 で渡す)
	CHECK(runtime::sdk_folder_for_module(L"C:\\\u958b\u767a\\Game.exe") == L"C:\\\u958b\u767a\\D3D12\\");
	// 長いパス(MAX_PATH を超える)も切らない
	const std::wstring long_folder{ L"C:\\" + std::wstring(300, L'a') + L"\\" };
	CHECK(runtime::sdk_folder_for_module(long_folder + L"Game.exe") == long_folder + L"D3D12\\");
	// フォルダの区切りがなければ、作業フォルダではなく exe からの相対パス(CreateDeviceFactory の解釈)にする
	CHECK(runtime::sdk_folder_for_module(L"Game.exe") == L".\\D3D12\\");
}

DXFORGE_TEST_CASE(D3D12Runtime_FormatsTheFileVersion)
{
	// D3D12Core.dll 1.620.1.0 の VS_FIXEDFILEINFO(dwFileVersionMS = 1 << 16 | 620、dwFileVersionLS = 1 << 16 | 0)
	CHECK(runtime::format_file_version((1u << 16) | 620u, (1u << 16) | 0u) == "1.620.1.0");
	CHECK(runtime::format_file_version(0xFFFFFFFFu, 0u) == "65535.65535.0.0");
}
