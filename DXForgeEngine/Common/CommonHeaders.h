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
// NOTE: std::vectorやstd::dequeをインクルードするヘッダーはここに書かないでください。
#include <stdint.h>
#include <assert.h>
#include <typeinfo>
#include <memory>
#include <unordered_map>
#include <mutex>

#if defined(_WIN64)
#include <DirectXMath.h>
#endif

// コピーを禁止するマクロ
#ifndef DISABLE_COPY
#define DISABLE_COPY(T)				\
	explicit T(const T&) = delete;	\
	T& operator=(const T&) = delete;
#endif // !DISABLE_COPY

// ムーブを禁止するマクロ
#ifndef DISABLE_MOVE
#define DISABLE_MOVE(T)				\
	explicit T(T&&) = delete;		\
	T& operator=(T&&) = delete;
#endif // !DISABLE_MOVE

// コピーとムーブを禁止するマクロ
#ifndef DISABLE_COPY_AND_MOVE
#define DISABLE_COPY_AND_MOVE(T) DISABLE_COPY(T) DISABLE_MOVE(T)
#endif // !DISABLE_COPY_AND_MOVE

// デバッグ時のみ処理を行うマクロ
#ifdef _DEBUG
#define DEBUG_OP(x) x
#else
#define DEBUG_OP(x)
#endif // _DEBUG

// ユーティリティ関数をまとめたファイル
#include "PrimitiveTypes.h"
#include "Utilities/Math.h"
#include "Utilities/Utilities.h"
#include "Utilities/MathType.h"

// ID型を定義したファイル
#include "Id.h"


