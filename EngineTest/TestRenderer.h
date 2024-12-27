// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [TestRenderer.h]
// 作成日 : 2024/12/27
// 作成者 : 田中ミノル
// 概要 :
// 　レンダラのテスト
// 更新履歴
// 2024/12/27 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "Test.h"

class engine_test : public test
{
public:
	bool initialize() override;
	void run() override;
	void shutdown() override;
};