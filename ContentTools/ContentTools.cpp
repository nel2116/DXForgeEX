// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ContentTools.cpp]
// 作成日 : 2025/01/26
// 作成者 : 田中ミノル
// 概要 :
//
// 更新履歴
// 2025/01/26 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "ToolsCommon.h"

namespace dxforge::tools
{
	extern void ShutDownTextureTools();
}

EDITOR_INTERFACE void ShutDownContentTools()
{
	using namespace dxforge::tools;
	ShutDownTextureTools();

}