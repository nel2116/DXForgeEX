// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Tests_ContentLoader.cpp]
// 作成日 : 2026/10/27
// 作成者 : 田中ミノル
// 概要 :
// Content/ContentLoader.cppのgame.binの読み込み(load_game/unload_game)のテスト
// 更新履歴
// 2026/10/27 新規作成 (#107, #112, #11)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "MicroTest.h"
#include "CommonHeaders.h"
#include "Content/ContentLoader.h"
#include "Components/Entity.h"
#include "Components/Script.h"
#include <filesystem>
#include <fstream>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

using namespace dxforge;

namespace
{
	// 生成・破棄の回数を数えるだけのスクリプト(途中で失敗したときに作成済みのエンティティが破棄されたかを見る)
	struct loader_counters
	{
		u32 created{ 0 };
		u32 destroyed{ 0 };
	};
	loader_counters counters{};

	class loader_test_counter : public script::entity_script
	{
	public:
		explicit loader_test_counter(game_entity::entity entity)
			: script::entity_script{ entity }
		{
			++counters.created;
		}

		~loader_test_counter() override
		{
			++counters.destroyed;
		}
	};

	// エディタ(Project.SaveToBinary)と同じ形式でgame.binを組み立てる
	class game_bin_builder
	{
	public:
		game_bin_builder& u32_value(u32 value) { append(&value, sizeof(value)); return *this; }
		game_bin_builder& f32_value(f32 value) { append(&value, sizeof(value)); return *this; }
		game_bin_builder& bytes(const std::string& text) { append(text.data(), text.size()); return *this; }

		// エンティティの見出し(エンティティ・タイプ、コンポーネント数)
		game_bin_builder& entity(u32 num_components) { return u32_value(0).u32_value(num_components); }

		// Transformコンポーネント(種類0、位置・回転(オイラー)・スケール)
		game_bin_builder& transform(f32 x, f32 y, f32 z)
		{
			u32_value(0);
			f32_value(x).f32_value(y).f32_value(z);
			f32_value(0.f).f32_value(0.f).f32_value(0.f);
			return f32_value(1.f).f32_value(1.f).f32_value(1.f);
		}

		// Scriptコンポーネント(種類1、名前の長さ(u32)と名前)
		game_bin_builder& script(const std::string& name)
		{
			return u32_value(1).u32_value((u32)name.size()).bytes(name);
		}

		const std::string& data() const { return _data; }

	private:
		void append(const void* data, size_t size) { _data.append((const char*)data, size); }
		std::string _data{};
	};

	// テストごとに%TEMP%の下に一時フォルダを作り、終わったら消す
	class temp_dir
	{
	public:
		temp_dir()
		{
			_path = std::filesystem::temp_directory_path() /
				(L"dxforge_loader_test_" + std::to_wstring(GetCurrentProcessId()) + L"_" + std::to_wstring(++_counter));
			std::filesystem::create_directories(_path);
		}

		~temp_dir()
		{
			std::error_code error{};
			std::filesystem::remove_all(_path, error);
		}

		std::filesystem::path write(const std::string& data, const wchar_t* name = L"game.bin") const
		{
			const std::filesystem::path file{ _path / name };
			std::ofstream out{ file, std::ios::out | std::ios::binary };
			out.write(data.data(), (std::streamsize)data.size());
			return file;
		}

		const std::filesystem::path& path() const { return _path; }

	private:
		std::filesystem::path _path{};
		static inline u32 _counter{ 0 };
	};

	content::load_game_result load(const game_bin_builder& builder)
	{
		temp_dir dir{};
		return content::load_game(dir.write(builder.data()));
	}
}

REGISTER_SCRIPT(loader_test_counter);

// #107: エンティティが0個のgame.binは空のシーンとして読み込める
DXFORGE_TEST_CASE(content_loader_zero_entities_loads_as_empty_scene)
{
	game_bin_builder bin{};
	bin.u32_value(0);
	CHECK(load(bin) == content::load_game_result::ok);
	CHECK(content::loaded_game_entities().empty());
	CHECK(content::last_load_game_error().empty());
	content::unload_game();
}

// エンティティが1個以上のgame.binは今までどおり読み込める
DXFORGE_TEST_CASE(content_loader_loads_entities_with_transform_and_script)
{
	const u32 created_before{ counters.created };
	game_bin_builder bin{};
	bin.u32_value(2);
	bin.entity(1).transform(1.f, 2.f, 3.f);
	bin.entity(2).transform(4.f, 5.f, 6.f).script("loader_test_counter");
	REQUIRE(load(bin) == content::load_game_result::ok);
	REQUIRE(content::loaded_game_entities().size() == 2);

	const game_entity::entity first{ content::loaded_game_entities()[0] };
	const game_entity::entity second{ content::loaded_game_entities()[1] };
	CHECK(first.position().x == 1.f);
	CHECK(first.position().y == 2.f);
	CHECK(first.position().z == 3.f);
	CHECK(!first.script().is_valid());
	CHECK(second.position().x == 4.f);
	CHECK(second.script().is_valid());
	CHECK(counters.created - created_before == 1);

	content::unload_game();
	CHECK(content::loaded_game_entities().empty());
	CHECK(!game_entity::is_alive(first.get_id()));
	CHECK(!game_entity::is_alive(second.get_id()));
}

// #112: 256以上の値(エンティティ数)を4バイトで正しく読む
DXFORGE_TEST_CASE(content_loader_reads_u32_counts_of_256_or_more)
{
	constexpr u32 count{ 300 };
	game_bin_builder bin{};
	bin.u32_value(count);
	for (u32 i{ 0 }; i < count; ++i) bin.entity(1).transform((f32)i, 0.f, 0.f);
	REQUIRE(load(bin) == content::load_game_result::ok);
	REQUIRE(content::loaded_game_entities().size() == count);
	CHECK(content::loaded_game_entities()[count - 1].position().x == (f32)(count - 1));
	content::unload_game();
}

// #112: スクリプト名の長さも4バイトで読む(256は1バイト読みだと0になり、上限を超えるのでエラー)
DXFORGE_TEST_CASE(content_loader_rejects_script_name_length_of_256)
{
	game_bin_builder bin{};
	bin.u32_value(1);
	bin.entity(2).transform(0.f, 0.f, 0.f).u32_value(1).u32_value(256).bytes(std::string(256, 'a'));
	CHECK(load(bin) == content::load_game_result::invalid_format);
	CHECK(content::loaded_game_entities().empty());
}

// #107: 途中で失敗したら、それまでに作成したエンティティを破棄する
DXFORGE_TEST_CASE(content_loader_failure_midway_removes_created_entities)
{
	const u32 created_before{ counters.created };
	const u32 destroyed_before{ counters.destroyed };
	game_bin_builder bin{};
	bin.u32_value(3);
	bin.entity(2).transform(0.f, 0.f, 0.f).script("loader_test_counter");
	bin.entity(2).transform(0.f, 0.f, 0.f).script("loader_test_counter");
	bin.entity(2).transform(0.f, 0.f, 0.f).script("loader_test_not_registered");
	CHECK(load(bin) == content::load_game_result::script_not_found);
	CHECK(content::last_load_game_error().find("loader_test_not_registered") != std::string::npos);
	CHECK(content::loaded_game_entities().empty());
	CHECK(counters.created - created_before == 2);
	CHECK(counters.destroyed - destroyed_before == 2);

	// 失敗した後も、もう一度読み込める
	game_bin_builder ok_bin{};
	ok_bin.u32_value(1);
	ok_bin.entity(1).transform(0.f, 0.f, 0.f);
	CHECK(load(ok_bin) == content::load_game_result::ok);
	content::unload_game();
}

DXFORGE_TEST_CASE(content_loader_reports_missing_and_empty_files)
{
	temp_dir dir{};
	CHECK(content::load_game(dir.path() / L"no_such_game.bin") == content::load_game_result::file_not_found);
	CHECK(content::load_game(dir.write({})) == content::load_game_result::read_failed);
	CHECK(content::loaded_game_entities().empty());
}

DXFORGE_TEST_CASE(content_loader_rejects_broken_files)
{
	{	// エンティティ数の途中で切れている
		game_bin_builder bin{};
		bin.bytes("\x01\x00");
		CHECK(load(bin) == content::load_game_result::invalid_format);
	}
	{	// エンティティ数より実際のエンティティが少ない
		game_bin_builder bin{};
		bin.u32_value(2);
		bin.entity(1).transform(0.f, 0.f, 0.f);
		CHECK(load(bin) == content::load_game_result::invalid_format);
	}
	{	// Transformの途中で切れている
		game_bin_builder bin{};
		bin.u32_value(1);
		bin.entity(1).u32_value(0).f32_value(1.f);
		CHECK(load(bin) == content::load_game_result::invalid_format);
	}
	{	// 最後のエンティティの後ろに余りのバイトがある
		game_bin_builder bin{};
		bin.u32_value(1);
		bin.entity(1).transform(0.f, 0.f, 0.f).u32_value(0xdeadbeef);
		CHECK(load(bin) == content::load_game_result::invalid_format);
	}
	{	// コンポーネントが0個
		game_bin_builder bin{};
		bin.u32_value(1);
		bin.entity(0);
		CHECK(load(bin) == content::load_game_result::invalid_format);
	}
	{	// Transformがない
		game_bin_builder bin{};
		bin.u32_value(1);
		bin.entity(1).script("loader_test_counter");
		CHECK(load(bin) == content::load_game_result::invalid_format);
	}
	{	// 同じコンポーネントが2つある
		game_bin_builder bin{};
		bin.u32_value(1);
		bin.entity(2).transform(0.f, 0.f, 0.f).transform(0.f, 0.f, 0.f);
		CHECK(load(bin) == content::load_game_result::invalid_format);
	}
	{	// 未知のコンポーネントの種類
		game_bin_builder bin{};
		bin.u32_value(1);
		bin.entity(2).transform(0.f, 0.f, 0.f).u32_value(7);
		CHECK(load(bin) == content::load_game_result::unknown_component);
	}
	{	// スクリプト名の長さが0
		game_bin_builder bin{};
		bin.u32_value(1);
		bin.entity(2).transform(0.f, 0.f, 0.f).u32_value(1).u32_value(0);
		CHECK(load(bin) == content::load_game_result::invalid_format);
	}
	CHECK(content::loaded_game_entities().empty());
}
