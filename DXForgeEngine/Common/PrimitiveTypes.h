// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [PrimitiveTypes.h]
// 作成日 : 2024/08/12
// 作成者 : 田中ミノル
// 概要
// 　基本的な型を定義したファイル
// 更新履歴
// 2024/08/12 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include <stdint.h>

// 符号なし整数型
using u64 = uint64_t;
using u32 = uint32_t;
using u16 = uint16_t;
using u8 = uint8_t;

// 符号付き整数型
using s64 = int64_t;
using s32 = int32_t;
using s16 = int16_t;
using s8 = int8_t;

// 無効なID
constexpr u64 u64_invalid_id(static_cast<u64>(0xffff'ffff'ffff'ffff));
constexpr u32 u32_invalid_id(static_cast<u32>(0xffff'ffff));
constexpr u16 u16_invalid_id(static_cast<u16>(0xffff));
constexpr u8 u8_invalid_id(static_cast<u8>(0xff));

// 浮動小数点型
using f32 = float;