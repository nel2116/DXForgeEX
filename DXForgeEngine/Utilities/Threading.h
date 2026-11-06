// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Threading.h]
// 作成日 : 2026/11/07
// 作成者 : 田中ミノル
// 概要
// 　スレッドの排他に使うユーティリティ(Primal Day 083)
// 更新履歴
// 2026/11/07 新規作成 (#209)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
#include "CommonHeaders.h"
#include <intrin.h>

namespace dxforge::utl
{
	// NOTE: Primal の _WIN64 以外の分岐(std::atomic)は移植しない。ソリューションの構成は x64 だけで、コンパイルもテストもされないため。
#if _WIN64
	/// @brief lock() を呼んだ順に入れるスピンロック
	/// @note 描画のスレッドが lock と unlock を繰り返していても、他のスレッドは順番が来れば必ず入れる。
	///       待つ間は CPU を使い続ける。同じスレッドから続けて lock() すると、永久に待つ。
	class ticket_mutex
	{
	public:
		ticket_mutex() = default;
		DISABLE_COPY_AND_MOVE(ticket_mutex);

		void lock()
		{
			const u64 ticket{ (u64)_InterlockedExchangeAdd64((__int64 volatile*)&_ticket, 1) };
			while (_serving != ticket) { _mm_pause(); }
		}

		void unlock()
		{
			_InterlockedExchangeAdd64((__int64 volatile*)&_serving, 1);
		}

	private:
		u64 volatile _ticket{ 0 };
		u64 volatile _serving{ 0 };
	};
#endif // _WIN64
}
