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
	using id_type = u32; // id_type を 32 ビット符号なし整数 (u32) として定義

	namespace internal
	{
		constexpr u32 generation_bits{ 8 }; // 世代番号を保持するために使用するビット数を 8 に設定
		// インデックスに使用するビット数を計算
		constexpr u32 index_bits{ sizeof(id_type) * 8 - generation_bits };
		// インデックスマスクを定義し、下位ビットをインデックスとして使用
		constexpr id_type index_mask{ (id_type{1} << index_bits) - 1 };
		// 世代マスクを定義し、上位ビットを世代番号として使用
		constexpr id_type generation_mask{ (id_type{1} << generation_bits) - 1 };
	}	// namespace internal
	// ID の最大値（無効な ID を示す）を定義
	constexpr id_type invalid_id{ (id_type)-1 };
	// 削除された要素の最小数を定義
	constexpr u32 min_deleted_elements{ 1024 };

	// generation_type を条件に基づいて決定 (generation_bits が 8 以下なら u8、16 以下なら u16、それ以外は u32)
	using generation_type = std::conditional_t<internal::generation_bits <= 8, u8, std::conditional_t<internal::generation_bits <= 16, u16, u32>>;

	// generation_type のサイズが、generation_bits によって必要なビット数を確保できることを確認する
	static_assert(sizeof(generation_type) * 8 >= internal::generation_bits);

	// id_type のサイズが、generation_type のサイズ以上であることを確認
	static_assert((sizeof(id_type) - sizeof(generation_type)) > 0);

	/// @brief ID が有効かどうかを判定する関数
	/// @param id 判定する ID
	/// @return ID が有効なら true、無効なら false
	inline bool is_valid(id_type id)
	{
		return id != invalid_id; // 無効な ID は invalid_id と一致するため、それ以外は有効とする
	}

	/// @brief ID のインデックス部分を取得する関数
	/// @param id インデックス部分を取得する ID
	/// @return ID のインデックス部分
	inline id_type index(id_type id)
	{
		id_type index{ id & internal::index_mask }; // ID のインデックス部分をマスクして取得
		assert(index != internal::index_mask); // インデックスが最大値でないことを確認
		return index;
	}

	/// @brief ID の世代部分を取得する関数
	/// @param id 世代部分を取得する ID
	/// @return ID の世代部分
	inline id_type generation(id_type id)
	{
		return (id >> internal::index_bits) & internal::generation_mask; // ID の世代部分を右シフトして取得し、世代マスクを適用
	}

	/// @brief ID の世代部分を更新する関数
	/// @param id 世代部分を更新する ID
	/// @return 新しい ID
	inline id_type new_generation(id_type id)
	{
		const id_type generation{ id::generation(id) + 1 }; // 現在の世代に 1 を加える
		assert(generation < (((u64)1 << internal::generation_bits) - 1));	// 世代番号が最大値に達していないことを確認
		return index(id) | (generation << internal::index_bits); // 新しい世代を生成し、インデックス部分と組み合わせて新しい ID を返す
	}

#if _DEBUG
	namespace internal
	{
		struct id_base
		{
			constexpr explicit id_base(id_type id) : _id{ id } {} // コンストラクタ: id_type 型の ID を受け取り、プライベートメンバ _id に設定
			constexpr operator id_type() const { return _id; }    // id_base を id_type に暗黙的に変換するオペレータ

		private:
			id_type _id;  // プライベートメンバ: ID を保持する
		};
	}	// namespace internal

#define DEFINE_TYPED_ID(name)								\
    struct name final : id::internal::id_base				\
    {														\
        constexpr explicit name(id::id_type id)				\
            : id_base{ id } {}								\
        constexpr name() : id_base{ 0 } {}					\
    };

#else
	// デバッグ以外のビルドでは、DEFINE_TYPED_ID マクロは単に id::id_type の別名を作成
#define DEFINE_TYPED_ID(name) using name = id::id_type;
#endif

}