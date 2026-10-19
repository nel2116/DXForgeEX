// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
/// @file IOStream.h
/// @brief バイナリデータの読み書きを行うユーティリティクラス
/// @author 田中ミノル
/// @date 2025/01/12
/// @details
/// - blob_stream_reader: バイナリデータを読み込むクラス
/// - blob_stream_writer: バイナリデータを書き込むクラス
/// @version 1.0
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"

namespace dxforge::utl
{
    /// @class blob_stream_reader
    /// @brief バイナリデータを読み込むためのユーティリティクラス
    /// @note このクラスはローカルな使用（1つの関数内での使用）のみを意図しています。
    ///       インスタンスをメンバ変数として保持しないでください。
    class blob_stream_reader
    {
    public:
        /// @brief コピーおよびムーブを禁止
        DISABLE_COPY_AND_MOVE(blob_stream_reader);

        /// @brief コンストラクタ
        /// @param buffer 読み込み対象のバッファ
        explicit blob_stream_reader(const u8* buffer)
            :_buffer{ buffer }, _position{ buffer }
        {
            assert(buffer);
        }

        /// @brief プリミティブ型を読み込む
        /// @tparam T プリミティブ型
        /// @return 読み込んだ値
        template<typename T>
        [[nodiscard]] constexpr T read()
        {
			// C++でプリミティブ型と呼ぶのかはわかりませんが、ここでは整数型や浮動小数点型を指すものとします(int, float, doubleなど)。
            static_assert(std::is_arithmetic_v<T>, "Template argument should be a primitive type.");
            T value{ *((T*)_position) };
            _position += sizeof(T);
            return value;
        }

        /// @brief バイトを読み込む
        /// @param buffer 読み込んだデータを格納するバッファ
        /// @param length 読み込むバイト数
        void read(u8* buffer, size_t length)
        {
            memcpy(buffer, _position, length);
            _position += length;
        }

        /// @brief バイトをスキップする
        /// @param offset スキップするバイト数
        constexpr void skip(size_t offset)
        {
            _position += offset;
        }

        /// @brief バッファの先頭を取得
        /// @return バッファの先頭アドレス
        [[nodiscard]] constexpr const u8* const buffer_start() const { return _buffer; }

        /// @brief 現在の位置を取得
        /// @return 現在の位置のアドレス
        [[nodiscard]] constexpr const u8* const position() const { return _position; }

        /// @brief 現在のオフセットを取得
        /// @return バッファ先頭からのオフセット
        [[nodiscard]] constexpr size_t offset() const { return _position - _buffer; }

    private:
        const u8* const _buffer;	///< バッファ
        const u8* _position;		///< バッファの現在位置
    };

    /// @class blob_stream_writer
    /// @brief バイナリデータを書き込むためのユーティリティクラス
    /// @note このクラスはローカルな使用（1つの関数内での使用）のみを意図しています。
    ///       インスタンスをメンバ変数として保持しないでください。
    class blob_stream_writer
    {
    public:
        /// @brief コピーおよびムーブを禁止
        DISABLE_COPY_AND_MOVE(blob_stream_writer);

        /// @brief コンストラクタ
        /// @param buffer 書き込み対象のバッファ
        /// @param buffer_size バッファのサイズ
        explicit blob_stream_writer(u8* buffer, size_t buffer_size)
            :_buffer{ buffer }, _position{ buffer }, _buffer_size{ buffer_size }
        {
            assert(buffer && buffer_size);
        }

        /// @brief プリミティブ型を書き込む
        /// @tparam T プリミティブ型
        /// @param value 書き込む値
        template<typename T>
        void write(T value)
        {
			// C++でプリミティブ型と呼ぶのかはわかりませんが、ここでは整数型や浮動小数点型を指すものとします(int, float, doubleなど)。
            static_assert(std::is_arithmetic_v<T>, "Template argument should be a primitve type.");
            assert(&_position[sizeof(T)] <= &_buffer[_buffer_size]);
            *((T*)_position) = value;
            _position += sizeof(T);
        }

        /// @brief 文字列を書き込む
        /// @param buffer 書き込む文字列
        /// @param length 書き込む文字数
        void write(const char* buffer, size_t length)
        {
            assert(&_position[length] <= &_buffer[_buffer_size]);
            memcpy(_position, buffer, length);
            _position += length;
        }

        /// @brief バイトを書き込む
        /// @param buffer 書き込むデータ
        /// @param length 書き込むバイト数
        void write(const u8* buffer, size_t length)
        {
            assert(&_position[length] <= &_buffer[_buffer_size]);
            memcpy(_position, buffer, length);
            _position += length;
        }

        /// @brief バイトをスキップする
        /// @param offset スキップするバイト数
        void skip(size_t offset)
        {
            assert(&_position[offset] <= &_buffer[_buffer_size]);
            _position += offset;
        }

        /// @brief バッファの先頭を取得
        /// @return バッファの先頭アドレス
        [[nodiscard]] constexpr const u8* const buffer_start() const { return _buffer; }

        /// @brief バッファの終端を取得
        /// @return バッファの終端アドレス
        [[nodiscard]] constexpr const u8* const buffer_end() const { return &_buffer[_buffer_size]; }

        /// @brief 現在の位置を取得
        /// @return 現在の位置のアドレス
        [[nodiscard]] constexpr const u8* const position() const { return _position; }

        /// @brief 現在のオフセットを取得
        /// @return バッファ先頭からのオフセット
        [[nodiscard]] constexpr size_t offset() const { return _position - _buffer; }

    private:
        u8* const _buffer;		///< バッファ
        u8* _position;			///< バッファの現在位置
        size_t _buffer_size;	///< バッファのサイズ
    };
}