// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_Threading.cpp]
// 作成日 : 2026/11/07
// 作成者 : 田中ミノル
// 概要 :
// Utilities/Threading.h の ticket_mutex のテスト (Primal Day 083, #209)
// 更新履歴
// 2026/11/07 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"
#include "CommonHeaders.h"
#include "Utilities/Threading.h"

#include <atomic>
#include <chrono>
#include <thread>

using namespace dxforge;

namespace
{
	using tested_mutex = utl::ticket_mutex;

	/// @brief 待っているスレッドと、unlock の直後に lock し直すスレッドの、どちらが先に入るかを 1 回試す
	/// @return 待っていたスレッドが先に入れば true
	bool waiting_thread_enters_first()
	{
		tested_mutex mutex{};
		std::atomic<bool> about_to_lock{ false };
		u32 first_to_enter{ 0 };	// mutex で守る

		std::unique_lock main_lock{ mutex };
		std::thread waiter{ [&]()
		{
			about_to_lock = true;
			std::lock_guard lock{ mutex };
			if (!first_to_enter) first_to_enter = 1;
		} };

		while (!about_to_lock) std::this_thread::yield();
		// NOTE: 待つ側が lock() の中に入るまでの時間。足りなければこの回は false になるだけで、呼び出し側が試し直す
		std::this_thread::sleep_for(std::chrono::milliseconds{ 50 });

		main_lock.unlock();
		main_lock.lock();
		if (!first_to_enter) first_to_enter = 2;
		main_lock.unlock();

		waiter.join();
		return first_to_enter == 1;
	}
}

DXFORGE_TEST_CASE(ticket_mutex_excludes_threads_incrementing_one_counter)
{
	constexpr u32 thread_count{ 4 };
	constexpr u32 increments_per_thread{ 10000 };

	tested_mutex mutex{};
	// NOTE: volatile にして読み書きを分けることで、排他がなければ増分が失われるようにする
	u64 volatile counter{ 0 };
	std::thread threads[thread_count];

	for (std::thread& thread : threads)
	{
		thread = std::thread{ [&]()
		{
			for (u32 i{ 0 }; i < increments_per_thread; ++i)
			{
				std::lock_guard lock{ mutex };
				const u64 value{ counter };
				counter = value + 1;
			}
		} };
	}
	for (std::thread& thread : threads) thread.join();

	CHECK(counter == (u64)thread_count * increments_per_thread);
}

// ticket_mutex があるのは、描画のスレッドが lock と unlock を繰り返しても他のスレッドが入れるようにするため(std::mutex には順番の保証がない)
DXFORGE_TEST_CASE(ticket_mutex_lets_the_waiting_thread_enter_before_a_relock)
{
	// NOTE: スケジューラの都合で 1 回は外れることがあるので、5 回のうち 1 回でも先に入れば合格にする
	bool entered_first{ false };
	for (u32 attempt{ 0 }; attempt < 5 && !entered_first; ++attempt)
	{
		entered_first = waiting_thread_enters_first();
	}
	CHECK(entered_first);
}
