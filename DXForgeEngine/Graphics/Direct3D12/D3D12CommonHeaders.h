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

#include <dxgi1_6.h>
#include <d3d12.h>
#include <wrl.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")

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
// COM オブジェクトの名前を設定し、デバッグ文字列を Visual Studio* の出力ウィンドウに出力します。
#define NAME_D3D12_OBJECT(obj,name) obj->SetName(name); OutputDebugString(L"::D3D12 Object Created: "); OutputDebugString(name); OutputDebugString(L"\n");
#else
#define NAME_D3D12_OBJECT(obj,name)
#endif // _DEBUG

