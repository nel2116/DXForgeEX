// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Main.cpp]
// 作成日 : 2026/10/18
// 作成者 : 田中ミノル
// 概要 :
// GPU・ウィンドウを使わずにCLI/CIから実行できる自動テストランナー。
// 更新履歴
// 2026/10/18 新規作成
// 2026/10/19 assert/abort()でダイアログを出さずに標準エラーへ出して終了するように変更(#89)。--interactiveを追加
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"

#include <crtdbg.h>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

namespace
{
	// 実行中のテストケース名。assertなどでabort()されたとき、どのケースで止まったかを出すために使う
	const char* running_case_name{ nullptr };

	void on_abort_signal(int)
	{
		// 状態を壊したあとに呼ばれるので、標準エラーへ1行出すだけにする(戻るとabort()がそのまま終了コード3で終える)
		std::fprintf(stderr, "[ABORT] %s\n", running_case_name ? running_case_name : "(outside of test case)");
		std::fflush(stderr);
	}

	// CI/CLIから無人で実行したとき、assert・abort()・クラッシュでダイアログを出して止まらないようにする。
	// assertの内容は標準エラーへ出し、プロセスはすぐに非ゼロ(abort()なら3)で終わる。
	void configure_unattended_error_reporting()
	{
		// assert()の報告先を標準エラーにする(既定ではメッセージボックスになる場合がある)
		_set_error_mode(_OUT_TO_STDERR);
		// abort()の「Debug Error! abort() has been called」ダイアログとWindowsエラー報告を出さない
		_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
		// デバッグCRTの_ASSERT/_ASSERTE/_RPTなどの報告先を標準エラーにする(Releaseでは何もしない)
		_CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
		_CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
		// アクセス違反などで出るWindowsのエラーダイアログも出さない
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		std::signal(SIGABRT, on_abort_signal);
	}

	struct case_result
	{
		std::string                                   name;
		std::vector<dxforge::test::check_failure>     failures;
	};

	// 1つのテストケースを実行し、その結果を返す
	case_result run_case(const dxforge::test::test_case& tc)
	{
		dxforge::test::current_failures().clear();
		try
		{
			tc.func();
		}
		catch (const dxforge::test::test_aborted&)
		{
			// REQUIRE失敗。失敗自体は既にrecord_failure()で記録済みなので、ここでは何もしない。
		}
		return { tc.name, dxforge::test::current_failures() };
	}

	void print_human_readable(const std::vector<case_result>& results, int passed, int total)
	{
		for (const case_result& result : results)
		{
			if (result.failures.empty())
			{
				std::printf("[PASS] %s\n", result.name.c_str());
			}
			else
			{
				std::printf("[FAIL] %s\n", result.name.c_str());
				for (const dxforge::test::check_failure& failure : result.failures)
				{
					std::printf("    %s(%d): %s\n", failure.file.c_str(), failure.line, failure.expression.c_str());
				}
			}
		}
		std::printf("----\n%d/%d passed\n", passed, total);
	}

	// JSON文字列中の制御文字・引用符をエスケープする
	std::string json_escape(const std::string& s)
	{
		std::string out;
		out.reserve(s.size());
		for (char c : s)
		{
			if (c == '"' || c == '\\') out += '\\';
			out += c;
		}
		return out;
	}

	void print_json(const std::vector<case_result>& results, int passed, int total)
	{
		std::printf("{\"passed\":%d,\"total\":%d,\"cases\":[", passed, total);
		for (size_t i{ 0 }; i < results.size(); ++i)
		{
			const case_result& result{ results[i] };
			std::printf("%s{\"name\":\"%s\",\"passed\":%s,\"failures\":[",
				i == 0 ? "" : ",",
				json_escape(result.name).c_str(),
				result.failures.empty() ? "true" : "false");
			for (size_t j{ 0 }; j < result.failures.size(); ++j)
			{
				const dxforge::test::check_failure& failure{ result.failures[j] };
				std::printf("%s{\"expression\":\"%s\",\"file\":\"%s\",\"line\":%d}",
					j == 0 ? "" : ",",
					json_escape(failure.expression).c_str(),
					json_escape(failure.file).c_str(),
					failure.line);
			}
			std::printf("]}");
		}
		std::printf("]}\n");
	}
} // 無名名前空間

int main(int argc, char** argv)
{
	bool as_json{ false };
	bool interactive{ false };
	for (int i{ 1 }; i < argc; ++i)
	{
		const std::string arg{ argv[i] };
		if (arg == "--json") as_json = true;
		else if (arg == "--interactive") interactive = true;	// 対話的にデバッグするため、CRT既定のダイアログを残す
	}

	if (!interactive) configure_unattended_error_reporting();

	std::vector<case_result> results;
	results.reserve(dxforge::test::registry().size());
	int passed{ 0 };

	for (const dxforge::test::test_case& tc : dxforge::test::registry())
	{
		running_case_name = tc.name.c_str();
		case_result result{ run_case(tc) };
		running_case_name = nullptr;
		if (result.failures.empty()) ++passed;
		results.push_back(std::move(result));
	}

	const int total{ (int)results.size() };
	if (as_json) print_json(results, passed, total);
	else print_human_readable(results, passed, total);

	return (passed == total) ? 0 : 1;
}
