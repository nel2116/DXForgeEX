// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [CommonHeaders.h]
// 作成日 : 2024/08/12
// 作成者 : 田中ミノル
// 概要
// 　共通ヘッダーをまとめたファイル
// 更新履歴
// 2024/08/12 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
#pragma warning(disable: 4530) // 例外警告を無効にする
// ====== インクルード部 ======
// C/C++標準ライブラリ
#include <stdint.h>
#include <assert.h>
#include <typeinfo>
#include <memory>

#if defined(_WIN64)
#include <DirectXMath.h>
#endif

// ユーティリティ関数をまとめたファイル
#include "..\Utilities\Utilities.h"
#include "..\Utilities\MathType.h"

// 基本的な型を定義したファイル
#include "PrimitiveTypes.h"