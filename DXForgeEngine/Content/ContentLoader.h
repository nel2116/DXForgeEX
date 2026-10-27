// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ContentLoader.h]
// 作成日 : 2024/08/20
// 作成者 : 田中ミノル
// 概要
//	コンテンツの読み込み
// 更新履歴
// 2024/08/20 新規作成
// 2026/10/27 load_game()に読み込むパスを渡せるようにし、失敗の理由を列挙型で返すようにした (#107, #11)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "../Components/ComponentsCommon.h"

#if !defined(SHIPPING) && defined(_WIN64)
#include <filesystem>
#include <string>

namespace dxforge::content
{
	// load_game()の結果
	enum class load_game_result : u32
	{
		ok,
		file_not_found,			// ファイルが存在しない
		read_failed,			// ファイルが読めない(空のファイルを含む)
		invalid_format,			// 形式が壊れている(途中で切れている、余りのバイトがある、値が範囲外)
		unknown_component,		// 未知のコンポーネントの種類
		script_not_found,		// スクリプトが登録されていない
		entity_creation_failed,	// エンティティを作成できなかった
	};

	/// @brief 結果の識別名(例: "script_not_found")を返す
	const char* to_string(load_game_result result);

	/// @brief game.binを読み込み、エンティティを作成する
	/// @details エンティティが0個のgame.binは、空のシーンとして成功する。
	///			 途中で失敗した場合、それまでに作成したエンティティは破棄される。
	/// @param path 読み込むファイルのパス(既定は作業ディレクトリのgame.bin)
	load_game_result load_game(const std::filesystem::path& path = "game.bin");
	/// @brief 最後に失敗したload_game()の理由の説明(英語)。成功したときは空。
	const std::string& last_load_game_error();
	/// @brief load_game()で作成したエンティティをすべて破棄する(何度呼んでもよい)
	void unload_game();
	/// @brief load_game()で作成したエンティティの一覧
	const utl::vector<game_entity::entity>& loaded_game_entities();
	bool load_engine_shaders(std::unique_ptr<u8[]>& shaders, u64& size);
}

#endif // !defined(SHIPPING)
