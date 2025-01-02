// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [FreeList.h]
// 作成日 : 2024/12/31
// 作成者 : 田中ミノル
// 概要 :
// フリーリスト
// 更新履歴
// 2024/12/31 新規作成
// // _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"


namespace dxforge::utl
{
#if USE_STL_VECTOR
#pragma message("WARNING: using utl::free_list with std::vector result in duplicate calls to class constructor!")
#endif

	// ====== クラス ======
	/// @brief フリーリスト
	/// @tparam T フリーリストに格納する型
	template<typename T>
	class free_list
	{
		static_assert(sizeof(T) >= sizeof(u32));	// T は u32 以上のサイズである必要がある

		// _/_/_/_/_/_/_/_/ メンバ関数 _/_/_/_/_/_/_/_/
	public:		// ====== パブリック関数 ======
		/// @brief コンストラクタ
		free_list() = default;
		explicit free_list(u32 count)
		{
			_array.reserve(count);
		}

		/// @brief デストラクタ
		~free_list()
		{
			assert(!_size);
#if USE_STL_VECTOR
			memset(_array.data(), 0, _array.size() * sizeof(T));
#endif
		}

		/// @brief 配列に要素を追加する
		/// @tparam ...param 追加する要素のコンストラクタ引数
		/// @param ...p 追加する要素のコンストラクタ引数
		/// @return 追加した要素の番号
		template<class... param>
		constexpr u32 add(param&&... p)
		{
			u32 id{ u32_invalid_id };
			if (_next_free_index == u32_invalid_id)
			{
				id = (u32)_array.size();
				_array.emplace_back(std::forward<param>(p)...);
			}
			else
			{
				id = _next_free_index;
				assert(id < _array.size() && already_removed(id));
				_next_free_index = *(const u32* const)std::addressof(_array[id]);
				new (std::addressof(_array[id])) T(std::forward<param>(p)...);
			}
			++_size;
			return id;
		}

		/// @brief 配列の要素を削除する
		/// @param id 削除する要素の要素番号
		constexpr void remove(u32 id)
		{
			assert(id < _array.size() && !already_removed(id));
			T& item{ _array[id] };
			item.~T();
			DEBUG_OP(memset(std::addressof(_array[id]), 0xcc, sizeof(T)));
			*(u32*)std::addressof(_array[id]) = _next_free_index;
			_next_free_index = id;
			--_size;
		}

		/// @brief 配列のサイズを取得する
		/// @return 配列のサイズ
		constexpr u32 size() const
		{
			return _size;
		}

		/// @brief 配列の容量を取得する
		constexpr u32 capacity() const
		{
			return (u32)_array.size();
		}

		/// @brief 配列が空かどうかを取得する
		/// @return 配列が空の場合 true, それ以外の場合 false
		constexpr bool empty() const
		{
			return _size == 0;
		}

		[[nodiscard]] constexpr T& operator[](u32 id)
		{
			assert(id < _array.size() && !already_removed(id));
			return _array[id];
		}

		[[nodiscard]] constexpr const T& operator[](u32 id) const
		{
			assert(id < _array.size() && !already_removed(id));
			return _array[id];
		}

	private:	// ====== プライベート関数 ======

		constexpr bool already_removed(u32 id)
		{
			// NOTE: sizeof(T)==sizeof(u32)の場合、アイテムがすでに削除されているかどうかをテストできない。
			if constexpr (sizeof(T) > sizeof(u32))
			{
				u32 i{ sizeof(u32) };	// 最初の4バイトをスキップする
				const u8* const p{ (const u8* const)std::addressof(_array[id]) };
				while ((p[i] == 0xcc) && (i < sizeof(T)))++i;
				return i == sizeof(T);
			}
			else
			{
				return true;
			}
		}

	private:	// ====== メンバ変数 ======
#if USE_STL_VECTOR
		std::vector<T> _array;						// 配列
#else
		utl::vector<T, false> _array;				// 配列
#endif
		u32 _next_free_index{ u32_invalid_id };		// 次の空きインデックス
		u32 _size{ 0 };								// 配列のサイズ
	};
}



