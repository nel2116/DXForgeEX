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

// ====== プリプロセッサ定義 ======
#ifdef _WIN64
// 例外に関する警告(C4530)を無効化
#pragma warning(disable: 4530)
#endif // _WIN64

// ====== インクルード部 ======
// C/C++標準ライブラリ
// NOTE: std::vectorやstd::dequeをインクルードするヘッダーはここに書かないでください。
#include <cstdint>   // 固定幅整数型のヘッダー
#include <assert.h>  // アサーション
#include <typeinfo>  // 型情報
#include <memory>    // スマートポインタ
#include <unordered_map> // ハッシュマップ
#include <mutex>     // 排他制御
#include <cstring>   // C文字列操作

#if defined(_WIN64)
#include <DirectXMath.h> // DirectXMathライブラリ (Windows 64bit環境のみ)
#endif

// ====== マクロ定義 ======
// コピーを禁止するマクロ
#ifndef DISABLE_COPY
#define DISABLE_COPY(T)             \
    explicit T(const T&) = delete;  \
    T& operator=(const T&) = delete;
#endif // !DISABLE_COPY

// ムーブを禁止するマクロ
#ifndef DISABLE_MOVE
#define DISABLE_MOVE(T)             \
    explicit T(T&&) = delete;       \
    T& operator=(T&&) = delete;
#endif // !DISABLE_MOVE

// コピーとムーブを禁止するマクロ
#ifndef DISABLE_COPY_AND_MOVE
#define DISABLE_COPY_AND_MOVE(T) DISABLE_COPY(T) DISABLE_MOVE(T)
#endif // !DISABLE_COPY_AND_MOVE

// デバッグ時のみ処理を行うマクロ
#ifdef _DEBUG
#define DEBUG_OP(x) x // デバッグビルド時にのみ有効化
#else
#define DEBUG_OP(x)
#endif // _DEBUG

// ====== ユーティリティ関数のインクルード ======
// 基本的な型を定義するファイル
#include "PrimitiveTypes.h"

// 数学関連のユーティリティ関数
#include "../Utilities/Math.h"
#include "../Utilities/Utilities.h"
#include "../Utilities/MathType.h"

// ID型を定義するファイル
#include "Id.h"
