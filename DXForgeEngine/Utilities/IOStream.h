// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [IOStream.h]
// 作成日 : 2025/01/12
// 作成者 : 田中ミノル
// 概要 :
//
// 更新履歴
// 2025/01/12 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"

namespace dxforge::utl
{
	// NOTE: (重要）このユーティリティ・クラスは、ローカルな使用（つまり1つの関数内での使用）のみを意図しています。
	//		インスタンスをメンバ変数として保持しないでください。
	class blob_stream_reader
	{
	public:	// パブリック関数
		DISABLE_COPY_AND_MOVE(blob_stream_reader);
		explicit blob_stream_reader(const u8* buffer)
			:_buffer{ buffer }, _position{ buffer }
		{
			assert(buffer);
		}

		// このテンプレート関数は、プリミティブ型（int、float、boolなど）を読み込むためのものです。
		/// @brief プリミティブ型を読み込む
		/// @tparam T プリミティブ型
		/// @return T 読み込んだ値
		template<typename T>
		[[nodiscard]] constexpr T read()
		{
			// C++ にprimitve型というものがあるかどうかはわかりません。これは整数型、FLOAT、DOUBLE、BOOL に対する私の呼び名です。
			static_assert(std::is_arithmetic_v<T>, "Template argument should be a primitve type.");
			T value{ *((T*)_position) };
			_position += sizeof(T);
			return value;
		}

		// 'length'バイトを'buffer'に読み込む。 呼び出し側は、buffer に十分なメモリを確保する責任がある。
		/// @brief バイトを読み込む
		/// @param buffer バッファ
		/// @param length 読み込むバイト数
		/// @return void
		void read(u8* buffer, size_t length)
		{
			memcpy(buffer, _position, length);
			_position += length;
		}

		// 'offset'バイトをスキップする。
		/// @brief バイトをスキップする
		/// @param offset スキップするバイト数
		constexpr void skip(size_t offset)
		{
			_position += offset;
		}

		// ====== アクセサ ======
		// バッファの先頭を取得
		[[nodiscard]] constexpr const u8* const buffer_start() const { return _buffer; }
		// バッファの終端を取得
		[[nodiscard]] constexpr const u8* const position() const { return _position; }
		// 現在の位置を取得
		[[nodiscard]] constexpr size_t offset() const { return _position - _buffer; }

	private:
		const u8* const _buffer;	///< バッファ
		const u8* _position;		///< バッファの位置
	};

	// NOTE: (重要）このユーティリティ・クラスは、ローカルな使用（つまり1つの関数内での使用）のみを意図しています。
	//		インスタンスをメンバ変数として保持しないでください。
	class blob_stream_writer
	{
	public:	// パブリック関数
		DISABLE_COPY_AND_MOVE(blob_stream_writer);
		explicit blob_stream_writer(u8* buffer, size_t buffer_size)
			:_buffer{ buffer }, _position{ buffer }, _buffer_size{ buffer_size }
		{
			assert(buffer && buffer_size);
		}

		// このテンプレート関数は、プリミティブ型（int、float、boolなど）を記述するためのものである。
		/// @brief プリミティブ型を書き込む
		/// @tparam T プリミティブ型
		/// @param value 書き込む値
		template<typename T>
		void write(T value)
		{
			// C++ にprimitve型というものがあるかどうかはわかりません。これは整数型、FLOAT、DOUBLE、BOOL に対する私の呼び名です。
			static_assert(std::is_arithmetic_v<T>, "Template argument should be a primitve type.");
			assert(&_position[sizeof(T)] <= &_buffer[_buffer_size]);
			*((T*)_position) = value;
			_position += sizeof(T);
		}

		// 'length' 文字を 'buffer' に書き込む。
		/// @brief 文字を書き込む
		/// @param buffer バッファ
		/// @param length 書き込む文字数
		void write(const char* buffer, size_t length)
		{
			assert(&_position[length] <= &_buffer[_buffer_size]);
			memcpy(_position, buffer, length);
			_position += length;
		}

		// 'length' バイトを 'buffer' に書き込む。
		/// @brief バイトを書き込む
		/// @param buffer バッファ
		/// @param length 書き込むバイト数
		void write(const u8* buffer, size_t length)
		{
			assert(&_position[length] <= &_buffer[_buffer_size]);
			memcpy(_position, buffer, length);
			_position += length;
		}

		// 'offset' バイトをスキップする。
		/// @brief バイトをスキップする
		/// @param offset スキップするバイト数
		void skip(size_t offset)
		{
			assert(&_position[offset] <= &_buffer[_buffer_size]);
			_position += offset;
		}

		// ====== アクセサ ======
		// バッファの先頭を取得
		[[nodiscard]] constexpr const u8* const buffer_start() const { return _buffer; }
		// バッファの終端を取得
		[[nodiscard]] constexpr const u8* const buffer_end() const { return &_buffer[_buffer_size]; }
		// 現在の位置を取得
		[[nodiscard]] constexpr const u8* const position() const { return _position; }
		// 現在のオフセットを取得
		[[nodiscard]] constexpr size_t offset() const { return _position - _buffer; }

	private:	// メンバ変数
		u8* const _buffer;		///< バッファ
		u8* _position;			///< バッファの位置
		size_t _buffer_size;	///< バッファのサイズ
	};
}