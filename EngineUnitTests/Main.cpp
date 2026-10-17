// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Main.cpp]
// 作成日 : 2026/10/18
// 作成者 : 田中ミノル
// 概要 :
// GPU・ウィンドウを使わずにCLI/CIから実行できる自動テストランナー。
// 更新履歴
// 2026/10/18 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"

#include <cstdio>
#include <string>

namespace
{
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
	for (int i{ 1 }; i < argc; ++i)
	{
		if (std::string{ argv[i] } == "--json") as_json = true;
	}

	std::vector<case_result> results;
	results.reserve(dxforge::test::registry().size());
	int passed{ 0 };

	for (const dxforge::test::test_case& tc : dxforge::test::registry())
	{
		case_result result{ run_case(tc) };
		if (result.failures.empty()) ++passed;
		results.push_back(std::move(result));
	}

	const int total{ (int)results.size() };
	if (as_json) print_json(results, passed, total);
	else print_human_readable(results, passed, total);

	return (passed == total) ? 0 : 1;
}
