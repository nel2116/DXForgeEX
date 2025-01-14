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
#include <chrono>
#include <string>

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
#if _WIN64
#include <Windows.h>
class time_it
{
public:
	using clock = std::chrono::high_resolution_clock;
	using time_stamp = std::chrono::steady_clock::time_point;

	// Average frame time per second.
	constexpr float dt_avg() const { return _dt_avg * 1e-6f; }

	void begin()
	{
		_start = clock::now();
	}

	void end()
	{
		auto dt = clock::now() - _start;
		_us_avg += ((float)std::chrono::duration_cast<std::chrono::microseconds>(dt).count() - _us_avg) / (float)_counter;
		++_counter;
		_dt_avg = _us_avg;

		if (std::chrono::duration_cast<std::chrono::seconds>(clock::now() - _seconds).count() >= 1)
		{
			OutputDebugStringA("Avg. frame (ms): ");
			OutputDebugStringA(std::to_string(_us_avg * 0.001f).c_str());
			OutputDebugStringA((" " + std::to_string(_counter)).c_str());
			OutputDebugStringA(" fps");
			OutputDebugStringA("\n");
			_us_avg = 0.f;
			_counter = 1;
			_seconds = clock::now();
		}
	}

private:
	float       _dt_avg{ 16.7f };
	float       _us_avg{ 0.f };
	int         _counter{ 1 };
	time_stamp  _start;
	time_stamp  _seconds{ clock::now() };
};

#endif // _WIN64