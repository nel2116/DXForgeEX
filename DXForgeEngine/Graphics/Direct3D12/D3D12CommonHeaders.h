// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [D3D12CommonHeaders.h]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要
// 　Direct3D12の共通ヘッダ
// 更新履歴
// 2024/12/27 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "Graphics/Renderer.h"
#include "Platform/Window.h"

// windows.hのmin/maxマクロの定義をスキップする。
#ifndef NOMINMAX
#define NOMINMAX
#endif // !NOMINMAX

#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")

namespace dxforge::graphics::d3d12
{
	constexpr u32 frame_buffer_count{ 3 };	// フレームバッファの数
	using id3d12_device = ID3D12Device8;
	using id3d12_graphics_command_list = ID3D12GraphicsCommandList6;
}

// ====== マクロ定義 ======
// DirectX12のエラーチェック
#ifdef _DEBUG
#ifndef DXCall
#define DXCall(x)							\
if(FAILED(x)){								\
char line_number[32];						\
sprintf_s(line_number, "%u", __LINE__);		\
OutputDebugStringA("Error in; ");			\
OutputDebugStringA(__FILE__);				\
OutputDebugStringA("\nLine");				\
OutputDebugStringA(line_number);			\
OutputDebugStringA("\n");					\
OutputDebugStringA(#x);						\
OutputDebugStringA("\n");					\
__debugbreak();								\
}
#endif // !DXCall
#else
#ifndef DXCall
#define DXCall(x) x
#endif // !DXCall
#endif // _DEBUG

#ifdef _DEBUG
// COM オブジェクトの名前を設定し、Visual Studio* の出力パネルにデバッグ文字列を出力します。
#define NAME_D3D12_OBJECT(obj, name) obj->SetName(name); OutputDebugString(L"::D3D12 Object Created: "); OutputDebugString(name); OutputDebugString(L"\n");
// インデックス付きバリアントは、オブジェクトの名前にインデックスを含む。
#define NAME_D3D12_OBJECT_INDEXED(obj, n, name)            \
{                                                          \
wchar_t full_name[128];                                    \
if (swprintf_s(full_name, L"%s[%llu]", name, (u64)n) >0 ){ \
    obj->SetName(full_name);                               \
    OutputDebugString(L"::D3D12 Object Created: ");        \
    OutputDebugString(full_name);                          \
    OutputDebugString(L"\n");                              \
}}
#else
#define NAME_D3D12_OBJECT(x, name)
#define NAME_D3D12_OBJECT_INDEXED(x, n, name)
#endif // _DEBUG

#include "D3D12Helpers.h"
#include "D3D12Resource.h"

