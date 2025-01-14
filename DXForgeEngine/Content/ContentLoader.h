// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ContentLoader.h]
// 作成日 : 2024/08/20
// 作成者 : 田中ミノル
// 概要
//	コンテンツの読み込み
// 更新履歴
// 2024/08/20 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"

#if !defined(SHIPPING) && defined(_WIN64)
namespace dxforge::content
{
	bool load_game();
	void unload_game();
	bool load_engine_shaders(std::unique_ptr<u8[]>& shaders, u64& size);
}

#endif // !defined(SHIPPING)

