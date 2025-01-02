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
#include <algorithm>
#define USE_STL_VECTOR 0
#define USE_STL_DEQUE 1
#include <algorithm>

#if USE_STL_VECTOR
#include <vector>
namespace dxforge::utl
{
	template<typename T>
	using vector = std::vector<T>;  // std::vector を primal::utl 名前空間内の vector としてエイリアス

	template<typename T>
	void erase_unordered(vector<T>& v, size_t index)
	{
		if (v.size() > 1)
		{
			std::iter_swap(v.begin() + index, v.end() - 1);
			v.pop_back();
		}
		else
		{
			v.clear();
		}
	}
}
#else
#include "Vector.h"

namespace dxforge::utl
{
	template<typename T>
	void erase_unordered(vector<T>& v, size_t index)
	{
		v.erase_unordered(index);
	}
}

#endif

#if USE_STL_DEQUE
#include <deque>
namespace dxforge::utl
{
	template<typename T>
	using deque = std::deque<T>;   // std::deque を primal::utl 名前空間内の deque としてエイリアス
}
#endif


namespace dxforge::util
{
	// TODO : 独自のコンテナを実装する
}

#include "FreeList.h"