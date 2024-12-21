// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [TestDll.h]
// 作成日 : 2024/12/21
// 作成者 : 田中ミノル
// 概要 :
// 　DLLのテスト
// 更新履歴
// 2024/12/21 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "Test.h"
#include <Windows.h>
#include <iostream>

extern "C" __declspec(dllimport) HWND DXForge_GetWindowHandle(unsigned int surfaceId);
extern "C" __declspec(dllimport) unsigned int CreateRenderSurface(HWND parent, int width, int height);
extern "C" __declspec(dllimport) void RemoveRenderSurface(unsigned int surfaceId);

class engine_test : public test
{
public:
	bool initialize() override
	{
		// ダミーのウィンドウハンドルを使用
		HWND dummyHwnd = (HWND)0x12345678;

		// サーフェイスを作成
		unsigned int surfaceId = CreateRenderSurface(dummyHwnd, 800, 600);
		if (surfaceId == 0)
		{
			std::cerr << "Failed to create render surface." << std::endl;
			return false;
		}

		std::cout << "Surface created with ID: " << surfaceId << std::endl;

		// GetWindowHandle関数をテスト
		HWND handle = DXForge_GetWindowHandle(surfaceId);
		if (handle != nullptr)
		{
			std::cout << "GetWindowHandle succeeded. Handle: " << handle << std::endl;
		}
		else
		{
			std::cerr << "GetWindowHandle failed." << std::endl;
		}

		// サーフェイスを削除
		RemoveRenderSurface(surfaceId);
		std::cout << "Surface removed." << std::endl;
		return true;
	}
	void run() override
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	void shutdown() override
	{

	}
private:
};