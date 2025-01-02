// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Vector.h]
// 作成日 : 2024/12/31
// 作成者 : 田中ミノル
// 概要 :
// ベクトル配列
// 更新履歴
// 2024/12/31 新規作成
// // _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"


namespace dxforge::utl
{
	// ====== クラス ======
	/// @brief ベクトル配列
	/// @tparam T ベクトル配列に格納する型
	/// std::vectorに似た配列クラスで、基本的な機能を持つ。
	/// ユーザーはテンプレート引数で、要素のデストラクタを削除時に呼び出すか
	/// 配列のクリア/デストラクタを呼び出すかを指定できる。
	template<typename T, bool destruct = true>
	class vector
	{
		// _/_/_/_/_/_/_/_/ メンバ関数 _/_/_/_/_/_/_/_/
	public:		// ====== パブリック関数 ======
		/// @brief デフォルトのコンストラクタ。 メモリを割り当てない。
		vector() = default;

		/// @brief 配列のサイズを変更するコンストラクタ
		/// @param count 配列のサイズ
		/// 'count' 要素を初期化します。
		constexpr explicit vector(u64 count)
		{
			resize(count);
		}

		/// @brief 配列のサイズを変更するコンストラクタ
		/// @param count 配列のサイズ
		/// @param value 初期化する値
		/// 'count' 要素を 'value' で初期化します。
		constexpr explicit vector(u64 count, const T& value)
		{
			resize(count, value);
		}

		template<typename it, typename = std::enable_if_t<std::_Is_iterator_v<it>>>
		constexpr explicit vector(it first, it last)
		{
			for (; first != last; ++first)
			{
				emplace_back(*first);
			}
		}

		/// @brief コピーコンストラクタ
		/// @param o コピー元の配列
		/// 別の配列をコピーして構築する。 コピーされた配列の項目はコピー可能でなければならない。
		constexpr vector(const vector& o)
		{
			*this = o;
		}

		/// @brief Moveコンストラクタ
		/// @param o コピー元の配列
		/// 別の配列を移動させて構築する。 移動後の元の配列は空になる。
		constexpr vector(const vector&& o)
			: _capacity{ o._capacity }, _size{ o._size }, _data{ o._data }
		{
			o.reset();
		}

		/// @brief コピー割り当て演算子
		/// この配列を消去し、別の配列から要素をコピーする。 要素はコピー可能でなければならない。
		constexpr vector& operator=(const vector& o)
		{
			assert(this != std::addressof(o));
			if (this != std::addressof(o))
			{
				clear();
				reserve(o._size);
				for (auto item : o)
				{
					emplace_back(item);
				}
				assert(_size == o._size);
			}
			return *this;
		}

		/// @brief Move割り当て演算子
		/// この配列を消去し、別の配列から要素を移動する。 要素は移動可能でなければならない。
		constexpr vector& operator=(vector&& o)
		{
			assert(this != std::addressof(o));
			if (this != std::addressof(o))
			{
				destroy();
				move(o);
			}
			return *this;
		}

		/// @brief デストラクタ
		/// テンプレート引数で指定されたベクトルとその要素を破棄する。
		~vector() { destroy(); }

		/// @brief valueをコピーして、配列の末尾に要素を挿入する。
		/// @param value 挿入する値
		constexpr void push_back(const T& value)
		{
			emplace_back(value);
		}

		/// @brief valueを移動して、配列の末尾に要素を挿入する。
		/// @param value 挿入する値
		constexpr void push_back(T&& value)
		{
			emplace_back(std::move(value));
		}

		template<typename... params>
		constexpr decltype(auto) emplace_back(params&&... args)
		{
			if (_size == _capacity)
			{
				reserve(((_capacity + 1) * 3) >> 1); // 50％増し
			}
			assert(_size < _capacity);

			new(std::addressof(_data[_size])) T(std::forward<params>(args)...);
			++_size;
			return _data[_size - 1];
		}


		/// @brief 配列のサイズを変更する。
		/// @param new_size 新しいサイズ
		/// 配列のサイズを変更し、新しい要素をデフォルト値で初期化する
		constexpr void resize(u64 new_size)
		{
			static_assert(std::is_default_constructible_v<T>, "Type must be default-constructible");

			if (new_size > _size)
			{
				reserve(new_size);
				while (_size < new_size)
				{
					emplace_back();
				}
			}
			else if (new_size < _size)
			{
				if constexpr (destruct)
				{
					destruct_range(new_size, _size);
				}
			}

			// new_size == _size の場合は何もしない。
			assert(new_size == _size);
		}

		/// @brief 配列のサイズを変更する。
		/// @param new_size 新しいサイズ
		/// 配列のサイズを変更し、新しい要素をコピーした値で初期化する
		constexpr void resize(u64 new_size, const T& value)
		{
			static_assert(std::is_copy_constructible_v<T>, "Type must be copy-constructible");

			if (new_size > _size)
			{
				reserve(new_size);
				while (_size < new_size)
				{
					emplace_back(value);
				}
			}
			else if (new_size < _size)
			{
				if constexpr (destruct)
				{
					destruct_range(new_size, _size);
				}
			}

			// new_size == _size の場合は何もしない。
			assert(new_size == _size);
		}

		/// @brief 指定された数の要素を格納するメモリを確保する。
		constexpr void reserve(u64 new_capacity)
		{
			if (new_capacity > _capacity)
			{
				// NOTE: realoc()は、新しいメモリ領域が割り当てられると、バッファ内のデータを自動的にコピーする。
				void* new_buffer{ realloc(_data, new_capacity * sizeof(T)) };
				assert(new_buffer);
				if (new_buffer)
				{
					_data = static_cast<T*>(new_buffer);
					_capacity = new_capacity;
				}
			}
		}

		/// @brief 指定されたインデックスの要素を削除します。
		/// @param index 削除する要素のインデックス
		constexpr T* const erase(u64 index)
		{
			assert(_data && index < _size);
			return erase(std::addressof(_data[index]));
		}

		/// @brief 指定した位置の要素を削除します。
		/// @param item 削除する要素の位置
		constexpr T* const erase(T* const item)
		{
			assert(_data && item >= std::addressof(_data[0]) && item < std::addressof(_data[_size]));
			if constexpr (destruct) item->~T();
			--_size;
			if (item < std::addressof(_data[_size]))
			{
				memcpy(item, item + 1, (std::addressof(_data[_size]) - item) * sizeof(T));
			}
			return item;
		}

		/// @brief 指定されたインデックスの要素を削除します。
		/// @param index 削除する要素のインデックス
		/// erase()と同じだが、最後の要素をコピーするだけなので高速。
		constexpr T* const erase_unordered(u64 index)
		{
			assert(_data && index < _size);
			return erase_unordered(std::addressof(_data[index]));
		}

		/// @brief 配列の指定した位置の要素を削除します。
		/// @param item 削除する要素の位置
		/// erase()と同じだが、最後の要素をコピーするだけなので高速。
		constexpr T* const erase_unordered(T* const item)
		{
			assert(_data && item >= std::addressof(_data[0]) && item < std::addressof(_data[_size]));
			if constexpr (destruct) item->~T();
			--_size;
			if (item < std::addressof(_data[_size]))
			{
				memcpy(item, std::addressof(_data[_size]), sizeof(T));
			}
			return item;
		}

		/// @brief 配列をクリアする。
		/// 配列をクリアし、テンプレート引数で指定されたデータを破棄します。
		constexpr void clear()
		{
			if constexpr (destruct)
			{
				destruct_range(0, _size);
			}
			_size = 0;
		}

		/// @brief 2つの配列を入れ替える。
		constexpr void swap(vector& o)
		{
			if (this != std::addressof(o))
			{
				auto temp{ o };
				o = *this;
				*this = temp;
			}
		}

		/// @brief データの開始位置へのポインタ。
		/// @return データの開始位置へのポインタ。 データがない場合はnullptrかもしれない。
		[[nodiscard]] constexpr T* data() { return _data; }

		/// @brief データの開始位置へのポインタ。
		/// @return データの開始位置へのポインタ。 データがない場合はnullptrかもしれない。
		[[nodiscard]] constexpr T* const data() const { return _data; }

		/// @brief 配列が空かどうかを返す。
		/// @return 配列が空の場合に真を返す。
		[[nodiscard]] constexpr bool empty() const { return _size == 0; }

		/// @brief 配列の要素数を返す。
		/// @return 配列内の要素数
		[[nodiscard]] constexpr u64 size() const { return _size; }

		/// @brief 配列の容量を返す。
		/// @return 配列の容量
		[[nodiscard]] constexpr u64 capacity() const { return _capacity; }

		/// @brief インデックス演算子。
		/// @param index 配列のインデックス
		/// 指定したインデックスの要素を返す。
		[[nodiscard]] constexpr T& operator[](u64 index)
		{
			assert(_data && index < _size);
			return _data[index];
		}

		/// @brief インデックス演算子。
		/// @param index 配列のインデックス
		/// 指定されたインデックスの要素への定数参照を返します。
		[[nodiscard]] constexpr const T& operator[](u64 index) const
		{
			assert(_data && index < _size);
			return _data[index];
		}

		/// @brief 配列の先頭要素への参照を返す。
		/// @return 配列の先頭要素への参照
		[[nodiscard]] constexpr T& front()
		{
			assert(_data && _size);
			return _data[0];
		}

		/// @brief 配列の先頭要素への定数参照を返す。
		/// @return 配列の先頭要素への定数参照
		[[nodiscard]] constexpr const T& front() const
		{
			assert(_data && _size);
			return _data[0];
		}

		/// @brief 配列の末尾要素への参照を返す。
		/// @return 配列の末尾要素への参照
		[[nodiscard]] constexpr T& back()
		{
			assert(_data && _size);
			return _data[_size - 1];
		}

		/// @brief 配列の末尾要素への定数参照を返す。
		/// @return 配列の末尾要素への定数参照
		[[nodiscard]] constexpr const T& back() const
		{
			assert(_data && _size);
			return _data[_size - 1];
		}

		/// @brief 最初の項目へのポインタを返す。
		///ベクトルが空の場合は null を返します。
		[[nodiscard]] constexpr T* begin()
		{
			assert(_data);
			return std::addressof(_data[0]);
		}

		/// @brief 最初の項目への定数ポインタを返す。
		///ベクトルが空の場合は null を返します。
		[[nodiscard]] constexpr const T* begin() const
		{
			assert(_data);
			return std::addressof(_data[0]);
		}

		/// @brief 最後の項目の次の項目へのポインタを返す。
		///ベクトルが空の場合は null を返します。
		[[nodiscard]] constexpr T* end()
		{
			assert(_data);
			return std::addressof(_data[_size]);
		}

		/// @brief 最後の項目の次の項目への定数ポインタを返す。
		///ベクトルが空の場合は null を返します。
		[[nodiscard]] constexpr const T* end() const
		{
			assert(_data);
			return std::addressof(_data[_size]);
		}

	private:	// ====== メンバ関数 ======
		constexpr void move(vector& o)
		{
			_capacity = o._capacity;
			_size = o._size;
			_data = o._data;
			o.reset();
		}

		constexpr void reset()
		{
			_capacity = 0;
			_size = 0;
			_data = nullptr;
		}

		constexpr void destruct_range(u64 first, u64 last)
		{
			assert(destruct);
			assert(first <= _size && last <= _size && first <= last);
			if (_data)
			{
				for (; first != last; ++first)
				{
					_data[first].~T();
				}
			}
		}

		constexpr void destroy()
		{
			assert([&] {return _capacity ? _data != nullptr : _data == nullptr; }());
			clear();
			_capacity = 0;
			if (_data) free(_data);
			_data = nullptr;
		}

	private:	// ====== メンバ変数 ======
		u64 _capacity{ 0 };		// 配列の容量
		u64 _size{ 0 };			// 配列のサイズ
		T* _data{ nullptr };	// 配列のデータ
	};
}
