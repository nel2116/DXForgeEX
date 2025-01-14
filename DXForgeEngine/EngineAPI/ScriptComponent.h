// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ScriptComponent.h]
// 作成日 : 2024/12/2
// 作成者 : 田中ミノル
// 概要 :
// 　スクリプトコンポーネント
// 更新履歴
// 2024/12/2 新規作成
// // _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "../Components/ComponentsCommon.h"

namespace dxforge::script
{
	DEFINE_TYPED_ID(script_id);

	class component final
	{
	public:
		constexpr explicit component(script_id id) : _id{ id } {}
		constexpr component() : _id{ id::invalid_id } {}
		constexpr script_id get_id() const { return _id; }
		constexpr bool is_valid() const { return id::is_valid(_id); }

	private:
		script_id _id;
	};
}






