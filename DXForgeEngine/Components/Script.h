// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Script.h]
// 作成日 : 2024/11/28
// 作成者 : 田中ミノル
// 概要
// 　Scriptコンポーネントを定義したファイル
// 更新履歴
// 2024/11/28 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "ComponentsCommon.h"

namespace dxforge::script
{
	struct init_info
	{
		detail::script_creator script_creator;
	};

	// Transformコンポーネントを作成する関数
	component create(init_info info, game_entity::entity entity);
	// Transformコンポーネントを削除する関数
	void remove(component c);

} // namespace dxforge::script
