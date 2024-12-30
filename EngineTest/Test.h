// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Test.h]
// 作成日 : 2024/11/11
// 作成者 : 田中ミノル
// 概要 :
// エンジンをテストするための基底クラスの定義
// 更新履歴
// 2024/11/11 新規作成
// 2024/13/20 thread.hを追加
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include <thread>

// ====== 定数 ======
#define TEST_ENTITY_COMPONENTS 0
#define TEST_WINDOW 0
#define TEST_DLL 0
#define TEST_RENDERER 1

// ====== クラスの定義 ======
class test
{
	virtual bool initialize() = 0;
	virtual void run() = 0;
	virtual void shutdown() = 0;
};
