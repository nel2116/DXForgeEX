// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Id.h]
// 作成日 : 2024/08/12
// 作成者 : 田中ミノル
// 概要
// 　IDを定義したファイル
// 更新履歴
// 2024/08/12 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"

namespace dxforge::id
{
	using id_type = u32;	///< IDの型

	namespace detail
	{
		constexpr u32 generation_bits{ 8 };											///< 8
		constexpr u32 index_bits{ sizeof(id_type) * 8 - generation_bits };			///< 24
		constexpr id_type index_mask{ (id_type{1} << index_bits) - 1 };				///< 16777215
		constexpr id_type generation_mask{ (id_type{1} << generation_bits) - 1 };	///< 255
	} // detail namespace

	constexpr id_type invalid_id{ id_type(-1) };	///< 無効なID
	constexpr u32 min_deleted_elements{ 1024 };		///< 最小削除要素数

	using generation_type = std::conditional_t<detail::generation_bits <= 16, std::conditional_t<detail::generation_bits <= 8, u8, u16>, u32>;	///< 世代の型
	static_assert(sizeof(generation_type) * 8 >= detail::generation_bits);	///< 世代のビット数が足りない場合はコンパイルエラー
	static_assert((sizeof(id_type) - sizeof(generation_type)) > 0);			///< 0より大きくなければならない

	constexpr generation_type max_generation{ (generation_type)(detail::generation_mask - 1) }; // 例：8ビット世代は254。

	/// @brief IDが有効かどうか
	/// @param id ID
	/// @return 有効ならtrue
	constexpr bool is_valid(id_type id)
	{
		return id != invalid_id;
	}

	/// @brief IDのインデックスを取得
	/// @param id ID
	/// @return インデックス
	constexpr id_type index(id_type id)
	{
		id_type index{ id & detail::index_mask };
		assert(index != detail::index_mask);
		return index;
	}

	/// @brief IDの世代を取得
	/// @param id ID
	/// @return 世代
	constexpr id_type generation(id_type id)
	{
		return (id >> detail::index_bits) & detail::generation_mask;
	}

	/// @brief IDの世代を更新
	/// @param id ID
	/// @retur 新しいID
	constexpr id_type new_generation(id_type id)
	{
		const id_type generation{ id::generation(id) + 1 };
		assert(generation < (((u64)1 << detail::generation_bits) - 1));
		return index(id) | (generation << detail::index_bits);
	}

#if _DEBUG
	namespace detail
	{
		struct id_base
		{
			constexpr explicit id_base(id_type id) : _id{ id } {}
			constexpr operator id_type() const { return _id; }
		private:
			id_type _id;
		};
	} // detail namespace

#define DEFINE_TYPED_ID(name)                                   \
        struct name final : id::detail::id_base                 \
        {                                                       \
            constexpr explicit name(id::id_type id)             \
                : id_base{ id } {}                              \
            constexpr name() : id_base{ 0 } {}                  \
        };
#else
#define DEFINE_TYPED_ID(name) using name = id::id_type;
#endif

}