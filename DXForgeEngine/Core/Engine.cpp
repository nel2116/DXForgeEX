// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Engine.cpp]
// 作成日 : 2024/12/20
// 作成者 : 田中ミノル
// 概要
//
// 更新履歴
// 2024/12/20 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// // ====== インクルード部 ======
#if !defined(SHIPPING)
#include "..\Content\ContentLoader.h"
#include "..\Components\Script.h"
#include <thread>

/// @brief エンジンの初期化
/// @return 初期化に成功した場合はtrueを返す
bool engine_initialize()
{
	bool  result{ dxforge::content::load_game() };
	return result;
}

/// @brief エンジンの更新
void engine_update()
{
	// 仮の処理
	dxforge::script::update(10.0f);
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

/// @brief エンジンの終了処理
void engine_shutdown()
{
	dxforge::content::unload_game();
}

#endif // !defined(SHIPPING)