// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Main.cpp]
// 作成日 : 2024/11/11
// 作成者 : 田中ミノル
// 概要 : 
// エンジンをテストするためのプログラム
// 更新履歴
// 2024/11/11 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "TestEntityComponent.h"
#pragma comment(lib, "DXForgeEngine.lib")

#define TEST_ENTITY_COMPONENTS 1

#if TEST_ENTITY_COMPONENTS

#else
#error いずれかのテストを有効にする必要があります
#endif	// TEST_ENTITY_COMPONENTS

int main()
{
#if _DEBUG
	// メモリリークチェック
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif // _DEBUG

	engine_test test{};

	if (test.initialize())
	{
		test.run();
	}

	test.shutdown();

	return 0;
}