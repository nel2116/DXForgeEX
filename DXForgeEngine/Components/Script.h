// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// // [Script.h]
// 作成日 : 2024/12/2
// 作成者 : 田中ミノル
// 概要
// 　スクリプトクラスを定義したファイル
// 更新履歴
// 2024/12/2 新規作成
// 2026/10/18 begin_play()の待ちリストの件数を返すdetail::pending_begin_play_count()を追加 (#47)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "ComponentsCommon.h"

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
	}
}