// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Input.h]
// 作成日 : 2025/01/18
// 作成者 : 田中ミノル
// 概要 :
// 入力のインターフェース
// 更新履歴
// 2025/01/18 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "EngineAPI/Input.h"

namespace dxforge::input
{
	void bind(input_source source);
	void unbind(input_source::type type, input_code::code code);
	void unbind(u64 binding);
	void set(input_source::type, input_code::code code, math::v3 value);
}
