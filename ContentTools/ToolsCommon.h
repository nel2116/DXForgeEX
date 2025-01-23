// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ToolsCommon.h]
// 作成日 : 2024/12/24
// 作成者 : 田中ミノル
// 概要 :
//
// 更新履歴
// 2024/12/24 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include <combaseapi.h>

// ====== マクロ定義 ======
#pragma once
#ifndef EDITOR_INTERFACE
#define EDITOR_INTERFACE extern "C" __declspec(dllexport)	// エディター用のインターフェースをエクスポート
#endif // EDITOR_INTERFACE

inline bool file_exists(const char* file)
{
	const DWORD attr{ GetFileAttributesA(file) };
	return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
}

inline std::wstring to_wstring(const char* cstr)
{
	std::string s{ cstr };
	return { s.begin(), s.end() };
}

inline dxforge::utl::vector<std::string> split(std::string s, char delimiter)
{
	size_t start{ 0 };
	size_t end{ 0 };
	std::string substring;
	dxforge::utl::vector<std::string> strings;

	while ((end = s.find(delimiter, start)) != std::string::npos)
	{
		substring = s.substr(start, end - start);
		start = end + sizeof(char);
		strings.emplace_back(substring);
	}

	strings.emplace_back(s.substr(start));
	return strings;
}
