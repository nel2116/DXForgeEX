// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Headless.h]
// 作成日 : 2026/10/27
// 作成者 : 田中ミノル
// 概要
//	ウィンドウもGPUも使わずに、game.binを読み込んでスクリプトの更新を指定フレーム数だけ回す実行経路
// 更新履歴
// 2026/10/27 新規作成 (#11)
// 2026/10/29 描画のヘッドレス実行(--render)の終了コードを追加 (#127)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"

#if !defined(SHIPPING) && defined(_WIN64)
namespace dxforge::headless
{
	// ヘッドレス実行の終了コード
	// NOTE: 1(ユニットテストの失敗)と3(abort())はEngineUnitTestsが使うので避けている。
	enum exit_code : int
	{
		success = 0,
		usage_error = 2,
		game_file_not_found = 10,
		game_file_read_failed = 11,
		game_file_invalid_format = 12,
		unknown_component = 13,
		script_not_found = 14,
		entity_creation_failed = 15,
		// 描画のヘッドレス実行(EngineUnitTests --render、#127)
		graphics_init_failed = 20,		// グラフィックスの初期化に失敗した(アダプタ・WARPが使えないなど)
		shaders_unavailable = 21,		// シェーダーを用意できない(DXCが見つからない、ソースがない、コンパイルに失敗した)
		render_failed = 22,				// 描画・読み戻しに失敗した(DebugビルドのデバッグレイヤーのERROR・CORRUPTIONを含む)
		screenshot_write_failed = 23,	// PNGを書き出せなかった
	};

	/// @brief 引数にヘッドレス実行の指定(--game-bin)が含まれているか
	bool is_requested(int argc, const wchar_t* const* argv);

	/// @brief 使い方を標準出力に書く
	void print_usage();

	/// @brief ヘッドレス実行を行う
	/// @details --game-bin <path> --frames <N> [--dt <seconds>] [--json]
	///			 結果は標準出力(--jsonならJSON)、ログは標準エラーに出す。
	/// @return 終了コード(exit_code)
	int run(int argc, const wchar_t* const* argv);
}
#endif // !defined(SHIPPING)
