// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// // [Script.h]
// 作成日 : 2024/12/2
// 作成者 : 田中ミノル
// 概要
// 　スクリプトクラスを定義したファイル
// 更新履歴
// 2024/12/2 新規作成
// 2026/10/18 begin_play()の待ちリストの件数を返すdetail::pending_begin_play_count()を追加 (#47)
// 2026/11/02 スクリプト名のSAFEARRAYをATLなしで作るdetail::make_script_names_safe_array()を追加 (#146)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "ComponentsCommon.h"
#include <string>

// <oleauto.h> の SAFEARRAY(typedef struct tagSAFEARRAY SAFEARRAY)。Windowsのヘッダーを広めないよう前方宣言にとどめる
struct tagSAFEARRAY;

namespace dxforge::script
{
	struct init_info
	{
		detail::script_creator script_creator;
	};

	component create(init_info info, game_entity::entity entity);
	void remove(component c);
	void update(f32 dt);

	namespace detail
	{
		/// begin_play()をまだ呼んでいないスクリプトの数(テスト・診断用)
		[[nodiscard]] u32 pending_begin_play_count();

		/// @brief スクリプト名の配列から、BSTRの1次元SAFEARRAY(下限0)を作る
		/// @param names スクリプト名(ANSIコードページ(CP_ACP)のバイト列として読む。エディタのANSIマーシャリングと往復させるため)
		/// @param count 名前の数
		/// @return 作ったSAFEARRAY。呼び出し側がSafeArrayDestroy()で解放する。countが0、または作れなかった場合はnullptr
		/// @note エディタ向けのget_script_names()が使う。ATLを使わずにOLE AutomationのAPIだけで作る(#146)
		[[nodiscard]] tagSAFEARRAY* make_script_names_safe_array(const std::string* names, u32 count);
	}
}