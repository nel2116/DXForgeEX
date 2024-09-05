// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Utilities.h]
// 作成日 : 2024/08/12
// 作成者 : 田中ミノル
// 概要
//
// 更新履歴
// 2024/08/12 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======

#define USE_STL_VECTOR 1
#define USE_STL_DEQUE 1

#if USE_STL_VECTOR
#include <vector>          // vector ヘッダーファイルをインクルード
namespace dxforge::utl
{
	template<typename T>
	using vector = std::vector<T>;  // std::vector を primal::utl 名前空間内の vector としてエイリアス
}
#endif

#if USE_STL_DEQUE
#include <deque>           // deque ヘッダーファイルをインクルード
namespace dxforge::utl
{
	template<typename T>
	using deque = std::deque<T>;   // std::deque を primal::utl 名前空間内の deque としてエイリアス
}
#endif


namespace dxforge::util
{
}