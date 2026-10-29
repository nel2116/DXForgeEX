// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [HeadlessRender.h]
// 作成日 : 2026/10/29
// 作成者 : 田中ミノル
// 概要
//	ウィンドウを作らずに固定のテストシーンを画面外のサーフェスに描画し、PNGに書き出す実行経路(--render)
// 更新履歴
// 2026/10/29 新規作成 (#127)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once

namespace dxforge::headless::render
{
	/// @brief 引数に描画のヘッドレス実行の指定(--render)が含まれているか
	bool is_requested(int argc, const wchar_t* const* argv);

	/// @brief 使い方を標準出力に書く
	void print_usage();

	/// @brief 描画のヘッドレス実行を行う
	/// @details --render --frames <N> --width <w> --height <h> --screenshot <png> [--adapter warp|hardware] [--json]
	///			 結果は標準出力(--jsonならJSON)、ログは標準エラーに出す。
	/// @return 終了コード(headless::exit_code)
	int run(int argc, const wchar_t* const* argv);
}
