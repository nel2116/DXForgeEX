// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [MicroTest.h]
// 作成日 : 2026/10/18
// 作成者 : 田中ミノル
// 概要 :
// 外部依存のない最小限の自動テストフレームワーク。
// GPU・ウィンドウを必要としない純粋ロジックをCLI/CIから検証するために使う。
// 更新履歴
// 2026/10/18 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include <string>
#include <vector>

namespace dxforge::test
{
	// ====== 構造体の定義 ======
	/// @brief REQUIREが失敗した際に、そのテストケースの残りの処理を中断するための例外
	struct test_aborted {};

	/// @brief 失敗したCHECK/REQUIREの情報
	struct check_failure
	{
		std::string expression;
		std::string file;
		int         line;
	};

	/// @brief 1つのテストケース
	struct test_case
	{
		std::string name;
		void (*func)();
	};

	// ====== グローバル状態 ======
	/// @brief 登録済みの全テストケース
	inline std::vector<test_case>& registry()
	{
		static std::vector<test_case> tests;
		return tests;
	}

	/// @brief 現在実行中のテストケースで発生した失敗の一覧
	inline std::vector<check_failure>& current_failures()
	{
		static std::vector<check_failure> failures;
		return failures;
	}

	/// @brief テストケースを登録するためのヘルパー(静的初期化時に呼ばれる)
	struct registrar
	{
		registrar(const char* name, void (*func)())
		{
			registry().push_back({ name, func });
		}
	};

	inline void record_failure(const char* expression, const char* file, int line)
	{
		current_failures().push_back({ expression, file, line });
	}
} // dxforge::test namespace

// ====== マクロ定義 ======
// テストケースを定義し、自動的にレジストリへ登録するマクロ
#define DXFORGE_TEST_CASE(name)                                                  \
	static void name();                                                         \
	namespace { const ::dxforge::test::registrar registrar_##name{ #name, &name }; } \
	static void name()

// 失敗してもテストケースの実行を継続するチェック
#define CHECK(expression)                                                       \
	do                                                                           \
	{                                                                            \
		if (!(expression))                                                      \
		{                                                                        \
			::dxforge::test::record_failure(#expression, __FILE__, __LINE__);   \
		}                                                                        \
	} while (0)

// 失敗した場合、そのテストケースの残りの処理を中断するチェック
#define REQUIRE(expression)                                                     \
	do                                                                           \
	{                                                                            \
		if (!(expression))                                                      \
		{                                                                        \
			::dxforge::test::record_failure(#expression, __FILE__, __LINE__);   \
			throw ::dxforge::test::test_aborted{};                              \
		}                                                                        \
	} while (0)
