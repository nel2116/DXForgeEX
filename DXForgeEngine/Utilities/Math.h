// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Math.h]
// 作成日 : 2024/12/24
// 作成者 : 田中ミノル
// 概要
//
// 更新履歴
// 2024/12/24 新規作成
// 2025/01/12 align_size_up()とalign_size_down()の追加
// 2025/01/19 is_equal()の追加
// 2025/04/15 コメントの追加
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"

// プラットフォーム固有のヘッダー
#if defined(_WIN64)
#include <DirectXMath.h>
#endif

#include "../Utilities/MathType.h"

namespace dxforge::math
{
	/// @brief 浮動小数点数の誤差を考慮した等価比較
	/// @param a １つ目の浮動小数点数
	/// @param b ２つ目の浮動小数点数
	/// @param eps 誤差の許容範囲
	/// @return aとbが等しい場合はtrue、そうでない場合はfalse
	/// @note epsはデフォルトで1e-6f
	[[nodiscard]] constexpr bool is_equal(f32 a, f32 b, f32 eps = epsilon)
	{
		f32 diff{ a - b };
		if (diff < 0.0f) diff = -diff;
		return diff < eps;
	}

	/// @brief 値を指定された範囲に制限する
	/// @param value 制限する値
	/// @param min 制限する最小値
	/// @param max 制限する最大値
	/// @return 制限された値
	/// @note min <= max であることを確認するためにアサートを使用
	/// @note minとmaxが同じ場合、valueはminまたはmaxに制限される
	template<typename T>
	[[nodiscard]] constexpr T clamp(T value, T min, T max)
	{
		assert(min <= max);
		return (value < min) ? min : (value > max) ? max : value;
	}

	/// @brief 値をビット数でパックする
	/// @tparam bits ビット数
	/// @param f 浮動小数点数
	/// @return パックされた値
	template<u32 bits>
	[[nodiscard]] constexpr u32 pack_unit_float(f32 f)
	{
		static_assert(bits && bits <= sizeof(u32) * 8);
		assert(f >= 0.f && f <= 1.f);
		constexpr f32 intervals{ (f32)(((u32)1 << bits) - 1) };
		return (u32)(intervals * f + 0.5f);
	}

	/// @brief パックされた値を浮動小数点数にアンパックする
	/// @tparam bits ビット数
	/// @param i パックされた値
	/// @return アンパックされた浮動小数点数
	template<u32 bits>
	[[nodiscard]] constexpr f32 unpack_to_unit_float(u32 i)
	{
		static_assert(bits && bits <= sizeof(u32) * 8);
		assert(i < ((u32)1 << bits));
		constexpr f32 intervals{ (f32)(((u32)1 << bits) - 1) };
		return (f32)i / intervals;
	}

	/// @brief 浮動小数点数を指定された範囲にパックする
	/// @tparam bits ビット数
	/// @param f 浮動小数点数
	/// @param min 制限する最小値
	/// @param max 制限する最大値
	/// @return パックされた値
	template<u32 bits>
	[[nodiscard]] constexpr u32 pack_float(f32 f, f32 min, f32 max)
	{
		assert(min < max);
		assert(f <= max && f >= min);
		const f32 distance{ (f - min) / (max - min) };
		return pack_unit_float<bits>(distance);
	}

	/// @brief パックされた値を指定された範囲の浮動小数点数にアンパックする
	/// @tparam bits ビット数
	/// @param i パックされた値
	/// @param min 制限する最小値
	/// @param max 制限する最大値
	/// @return アンパックされた浮動小数点数
	template<u32 bits>
	[[nodiscard]] constexpr f32 unpack_to_float(u32 i, f32 min, f32 max)
	{
		assert(min < max);
		return unpack_to_unit_float<bits>(i) * (max - min) + min;
	}

	/// @brief 四捨五入して整列する。 alignment' の倍数が 'size' 以上となる。
	/// @tparam alignment 整列サイズ
	/// @param size サイズ
	/// @return 整列されたサイズ
	/// @note alignmentは2の累乗である必要があります。
	template<u64 alignment>
	[[nodiscard]] constexpr u64 align_size_up(u64 size)
	{
		static_assert(alignment, "Alignment must be non-zero.");
		constexpr u64 mask{ alignment - 1 };
		static_assert(!(alignment & mask), "Alignment should be a power of 2.");
		return ((size + mask) & ~mask);
	}

	/// @brief 四捨五入して揃える。 alignment'の倍数が'size'以下になる。
	/// @tparam alignment 整列サイズ
	/// @param size サイズ
	/// @return 整列されたサイズ
	template<u64 alignment>
	[[nodiscard]] constexpr u64 align_size_down(u64 size)
	{
		static_assert(alignment, "Alignment must be non-zero.");
		constexpr u64 mask{ alignment - 1 };
		static_assert(!(alignment & mask), "Alignment should be a power of 2.");
		return (size & ~mask);
	}

	/// @brief 四捨五入して整列する。 alignment' の倍数が 'size' 以上となる。
	/// @param size サイズ
	/// @param alignment 整列サイズ
	/// @return 整列されたサイズ
	[[nodiscard]] constexpr u64 align_size_up(u64 size, u64 alignment)
	{
		assert(alignment && "Alignment must be non-zero.");
		const u64 mask{ alignment - 1 };
		assert(!(alignment & mask) && "Alignment should be a power of 2.");
		return ((size + mask) & ~mask);
	}

	/// @brief 四捨五入して揃える。 alignment'の倍数が'size'以下になる。
	/// @param size サイズ
	/// @param alignment 整列サイズ
	/// @return 整列されたサイズ
	[[nodiscard]] constexpr u64 align_size_down(u64 size, u64 alignment)
	{
		assert(alignment && "Alignment must be non-zero.");
		const u64 mask{ alignment - 1 };
		assert(!(alignment & mask) && "Alignment should be a power of 2.");
		return (size & ~mask);
	}

	/// @brief CRC32を計算する
	/// @param data データ
	/// @param size データサイズ
	/// @return CRC32の値
	[[nodiscard]] constexpr u64 calc_crc32_u64(const u8* const data, u64 size)
	{
		assert(size >= sizeof(u64));
		u64 crc{ 0 };
		const u8* at{ data };
		const u8* const end{ data + align_size_down<sizeof(u64)>(size) };
		while (at < end)
		{
			crc = _mm_crc32_u64(crc, *((const u64*)at));
			at += sizeof(u64);
		}

		return crc;
	}
}	// namespace dxforge::math

