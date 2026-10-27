// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Headless.cpp]
// 作成日 : 2026/10/27
// 作成者 : 田中ミノル
// 概要
//	ウィンドウもGPUも使わずに、game.binを読み込んでスクリプトの更新を指定フレーム数だけ回す実行経路の実装
// 更新履歴
// 2026/10/27 新規作成 (#11)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "Headless.h"
#include "Content/ContentLoader.h"
#include "Components/Entity.h"
#include "Components/Script.h"

#if !defined(SHIPPING) && defined(_WIN64)
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

namespace dxforge::headless
{
	namespace
	{
		// 引数の解析結果
		struct options
		{
			std::wstring	game_bin{};
			u32				frames{ 0 };
			f32				dt{ 0.016f };
			bool			has_game_bin{ false };
			bool			has_frames{ false };
			bool			as_json{ false };
		};

		// 実行結果
		struct run_result
		{
			int					exit_code{ exit_code::success };
			std::string			error_code{};		// 失敗したときの識別名(例: "script_not_found")
			std::string			error_message{};	// 失敗したときの説明
			u32					frames_run{ 0 };
			utl::vector<game_entity::entity_id>	entity_ids{};
			utl::vector<math::v3>	positions{};
			utl::vector<math::v4>	rotations{};
			utl::vector<math::v3>	scales{};
		};

		/// @brief UTF-16の文字列をUTF-8に変換する
		std::string to_utf8(const std::wstring& text)
		{
			if (text.empty()) return {};
			const int length{ WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr) };
			std::string result((size_t)length, '\0');
			WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), result.data(), length, nullptr, nullptr);
			return result;
		}

		/// @brief JSONの文字列として書けるようにエスケープする(UTF-8のまま)
		std::string escape_json(const std::string& text)
		{
			std::string result{};
			result.reserve(text.size());
			for (const char c : text)
			{
				switch (c)
				{
				case '"':	result += "\\\""; break;
				case '\\':	result += "\\\\"; break;
				case '\n':	result += "\\n"; break;
				case '\r':	result += "\\r"; break;
				case '\t':	result += "\\t"; break;
				default:
					if ((unsigned char)c < 0x20)
					{
						char buffer[8];
						std::snprintf(buffer, sizeof(buffer), "\\u%04x", (unsigned)c);
						result += buffer;
					}
					else
					{
						result += c;
					}
					break;
				}
			}
			return result;
		}

		/// @brief JSONの数値を書く(有限でない値はJSONで表せないのでnullにする)
		void print_json_number(f32 value)
		{
			if (std::isfinite(value)) std::printf("%.9g", value);
			else std::printf("null");
		}

		void print_json_vector(const f32* values, u32 count)
		{
			std::printf("[");
			for (u32 i{ 0 }; i < count; ++i)
			{
				if (i) std::printf(",");
				print_json_number(values[i]);
			}
			std::printf("]");
		}

		/// @brief 符号なし整数を引数として解析する(全体が数字でなければ失敗)
		bool parse_u32(const wchar_t* text, u32& value)
		{
			if (!text || !*text || *text == L'-' || *text == L'+') return false;
			wchar_t* end{ nullptr };
			errno = 0;
			const unsigned long long parsed{ std::wcstoull(text, &end, 10) };
			if (errno || *end || parsed > 0xffff'ffffull) return false;
			value = (u32)parsed;
			return true;
		}

		/// @brief 0以上の有限な実数を引数として解析する
		bool parse_dt(const wchar_t* text, f32& value)
		{
			if (!text || !*text) return false;
			wchar_t* end{ nullptr };
			errno = 0;
			const f32 parsed{ std::wcstof(text, &end) };
			if (errno || *end || !std::isfinite(parsed) || parsed < 0.f) return false;
			value = parsed;
			return true;
		}

		/// @brief 引数を解析する。失敗したらエラーの説明を返す
		std::string parse_options(int argc, const wchar_t* const* argv, options& opt)
		{
			// 引数の誤りもJSONで返せるように、--jsonは先に探しておく
			for (int i{ 1 }; i < argc; ++i)
			{
				if (std::wcscmp(argv[i], L"--json") == 0) opt.as_json = true;
			}
			for (int i{ 1 }; i < argc; ++i)
			{
				const std::wstring arg{ argv[i] };
				const bool has_value{ i + 1 < argc };
				if (arg == L"--json")
				{
					opt.as_json = true;
				}
				else if (arg == L"--game-bin")
				{
					if (!has_value) return "--game-bin requires a path";
					opt.game_bin = argv[++i];
					opt.has_game_bin = true;
				}
				else if (arg == L"--frames")
				{
					if (!has_value || !parse_u32(argv[i + 1], opt.frames)) return "--frames requires a non-negative integer";
					++i;
					opt.has_frames = true;
				}
				else if (arg == L"--dt")
				{
					if (!has_value || !parse_dt(argv[i + 1], opt.dt)) return "--dt requires a non-negative finite number";
					++i;
				}
				else
				{
					return "unknown argument: " + to_utf8(arg);
				}
			}
			if (!opt.has_game_bin || opt.game_bin.empty()) return "--game-bin is required";
			if (!opt.has_frames) return "--frames is required";
			return {};
		}

		int to_exit_code(content::load_game_result result)
		{
			switch (result)
			{
			case content::load_game_result::ok:						return exit_code::success;
			case content::load_game_result::file_not_found:			return exit_code::game_file_not_found;
			case content::load_game_result::read_failed:			return exit_code::game_file_read_failed;
			case content::load_game_result::invalid_format:			return exit_code::game_file_invalid_format;
			case content::load_game_result::unknown_component:		return exit_code::unknown_component;
			case content::load_game_result::script_not_found:		return exit_code::script_not_found;
			case content::load_game_result::entity_creation_failed:	return exit_code::entity_creation_failed;
			}
			return exit_code::game_file_invalid_format;
		}

		/// @brief game.binを読み込み、スクリプトの更新を回し、最後のフレームの状態を集める
		run_result execute(const options& opt)
		{
			run_result result{};
			std::fprintf(stderr, "[headless] loading %s\n", to_utf8(opt.game_bin).c_str());
			const content::load_game_result loaded{ content::load_game(opt.game_bin) };
			if (loaded != content::load_game_result::ok)
			{
				result.exit_code = to_exit_code(loaded);
				result.error_code = content::to_string(loaded);
				result.error_message = content::last_load_game_error();
				std::fprintf(stderr, "[headless] failed to load game: %s (%s)\n", result.error_code.c_str(), result.error_message.c_str());
				return result;
			}
			std::fprintf(stderr, "[headless] loaded %u entities\n", (u32)content::loaded_game_entities().size());

			// エンジンの更新(ゲームのexeのengine_update()から描画と待ち時間を除いたもの)
			for (u32 frame{ 0 }; frame < opt.frames; ++frame)
			{
				script::update(opt.dt);
				++result.frames_run;
			}

			// 最後のフレームの状態を集める(スクリプトに削除されたエンティティは除く)
			for (const game_entity::entity& entity : content::loaded_game_entities())
			{
				if (!game_entity::is_alive(entity.get_id())) continue;
				result.entity_ids.emplace_back(entity.get_id());
				result.positions.emplace_back(entity.position());
				result.rotations.emplace_back(entity.rotation());
				result.scales.emplace_back(entity.scale());
			}

			content::unload_game();
			std::fprintf(stderr, "[headless] ran %u frames\n", result.frames_run);
			return result;
		}

		void print_json_result(const options& opt, const run_result& result, u32 entity_count)
		{
			std::printf("{\"mode\":\"headless\",\"game_bin\":\"%s\",", escape_json(to_utf8(opt.game_bin)).c_str());
			std::printf("\"frames_requested\":%u,\"frames_run\":%u,\"dt\":", opt.frames, result.frames_run);
			print_json_number(opt.dt);
			std::printf(",\"success\":%s,\"exit_code\":%d,\"error\":", result.exit_code == exit_code::success ? "true" : "false", result.exit_code);
			if (result.exit_code == exit_code::success)
			{
				std::printf("null");
			}
			else
			{
				std::printf("{\"code\":\"%s\",\"message\":\"%s\"}",
					escape_json(result.error_code).c_str(), escape_json(result.error_message).c_str());
			}
			std::printf(",\"entity_count\":%u,\"entities\":[", entity_count);
			for (u32 i{ 0 }; i < (u32)result.entity_ids.size(); ++i)
			{
				if (i) std::printf(",");
				std::printf("{\"id\":%u,\"position\":", (u32)result.entity_ids[i]);
				print_json_vector(&result.positions[i].x, 3);
				std::printf(",\"rotation\":");
				print_json_vector(&result.rotations[i].x, 4);
				std::printf(",\"scale\":");
				print_json_vector(&result.scales[i].x, 3);
				std::printf("}");
			}
			std::printf("]}\n");
		}

		void print_human_result(const run_result& result, u32 entity_count)
		{
			if (result.exit_code != exit_code::success)
			{
				std::printf("headless: FAILED (exit code %d) %s: %s\n", result.exit_code, result.error_code.c_str(), result.error_message.c_str());
				return;
			}
			std::printf("headless: OK, %u frames, %u entities\n", result.frames_run, entity_count);
			for (u32 i{ 0 }; i < (u32)result.entity_ids.size(); ++i)
			{
				const math::v3& p{ result.positions[i] };
				std::printf("  entity %u: position (%g, %g, %g)\n", (u32)result.entity_ids[i], p.x, p.y, p.z);
			}
		}
	}	// 匿名名前空間

	bool is_requested(int argc, const wchar_t* const* argv)
	{
		for (int i{ 1 }; i < argc; ++i)
		{
			if (std::wcscmp(argv[i], L"--game-bin") == 0) return true;
		}
		return false;
	}

	void print_usage()
	{
		std::printf(
			"Headless run (no window, no GPU):\n"
			"  --game-bin <path>  game.bin to load (required)\n"
			"  --frames <N>       number of script::update() calls (required, N >= 0)\n"
			"  --dt <seconds>     delta time passed to each update (default 0.016)\n"
			"  --json             print a machine-readable result to stdout (logs go to stderr)\n"
			"Exit codes:\n"
			"  0  success (a game.bin with 0 entities is an empty scene and also succeeds)\n"
			"  2  invalid arguments\n"
			"  3  abort() (assert failure etc.)\n"
			"  10 game.bin not found\n"
			"  11 game.bin could not be read (or is empty)\n"
			"  12 game.bin has an invalid format (truncated, trailing bytes, out-of-range value)\n"
			"  13 unknown component type\n"
			"  14 script is not registered\n"
			"  15 entity creation failed\n");
	}

	int run(int argc, const wchar_t* const* argv)
	{
		options opt{};
		const std::string usage_error{ parse_options(argc, argv, opt) };
		run_result result{};
		if (!usage_error.empty())
		{
			result.exit_code = exit_code::usage_error;
			result.error_code = "usage_error";
			result.error_message = usage_error;
			std::fprintf(stderr, "[headless] %s\n", usage_error.c_str());
		}
		else
		{
			result = execute(opt);
		}

		const u32 entity_count{ (u32)result.entity_ids.size() };
		if (opt.as_json) print_json_result(opt, result, entity_count);
		else
		{
			print_human_result(result, entity_count);
			if (result.exit_code == exit_code::usage_error) print_usage();
		}
		std::fflush(stdout);
		return result.exit_code;
	}
}
#endif // !defined(SHIPPING)
