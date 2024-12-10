// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [EngineAPI.cpp]
// 作成日 : 2024/11/14
// 作成者 : 田中ミノル
// 概要 :
// エンジンのAPI
// 更新履歴
// 2024/11/14 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Common.h"
#include "CommonHeaders.h"

#ifdef WIN32_MEAN_AND_LEAN
#define WIN32_MEAN_AND_LEAN
#endif // WIN32_MEAN_AND_LEAN


#include <Windows.h>

using namespace dxforge;

namespace
{
	HMODULE game_code_dll{ nullptr };
} // 匿名名前空間

EDITOR_INTERFACE u32 LoadGameCodeDll(const char* dll_path)
{
	if (game_code_dll) return FALSE;
	game_code_dll = LoadLibraryA(dll_path);
	assert(game_code_dll);

	return game_code_dll ? TRUE : FALSE;
}

EDITOR_INTERFACE u32 UnloadGameCodeDll()
{
	if (!game_code_dll) return FALSE;
	assert(game_code_dll);
	int result{ FreeLibrary(game_code_dll) };
	assert(result);
	game_code_dll = nullptr;
	return TRUE;
}




