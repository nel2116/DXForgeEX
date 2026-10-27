// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ContentLoader.cpp]
// 作成日 : 2024/08/20
// 作成者 : 田中ミノル
// 概要
//	コンテンツの読み込みの実装
// 更新履歴
// 2024/08/20 新規作成
// 2026/10/27 u32の値を1バイトで読んでいたのを直し、壊れたgame.binを範囲外読み・assertではなくエラーとして扱うようにした (#112)
// 2026/10/27 エンティティが0個のgame.binを空のシーンとして読み込み、途中で失敗したら作成済みのエンティティを破棄するようにした (#107)
// 2026/10/27 読み込むパスを指定できるようにし、失敗の理由を返すようにした (#11)
// 2026/10/27 エラーメッセージに入れるスクリプト名の、表示できないASCII以外のバイトを\xNNにした(不正なUTF-8が--jsonに入っていたため) (#11)
// 2026/10/27 ファイルの存在の確認自体に失敗したときは、file_not_foundではなくread_failedにして理由を返すようにした (#11)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "ContentLoader.h"
#include "Components/Entity.h"
#include "Components/Transform.h"
#include "Components/Script.h"
#include "Graphics/Renderer.h"

#if !defined(SHIPPING) && defined(_WIN64)
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <Windows.h>

namespace dxforge::content
{
	namespace
	{
		// コンポーネントの種類
		enum conponent_type
		{
			transform,
			script,

			count
		};

		utl::vector < game_entity::entity > entities;	// エンティティのリスト
		transform::init_info transform_info{};			// Transformの初期化情報
		script::init_info script_info{};				// Scriptの初期化情報
		std::string last_error{};						// 最後に失敗したload_game()の理由

		// game.binのデータを、範囲を確かめながら先頭から読む
		// NOTE: 壊れたファイルで範囲外を読まないように、すべての読み込みをここで行う。
		class blob_reader
		{
		public:
			blob_reader(const u8* data, u64 size) : _at{ data }, _end{ data + size } {}

			// sizeバイトをdstへ読む。残りが足りなければfalse
			[[nodiscard]] bool read(void* dst, u64 size)
			{
				if (remaining() < size) return false;
				memcpy(dst, _at, size);
				_at += size;
				return true;
			}

			// u32を1つ読む(エディタはBinaryWriter.Write(int)でリトルエンディアンの4バイトを書く)
			[[nodiscard]] bool read_u32(u32& value) { return read(&value, sizeof(u32)); }

			[[nodiscard]] u64 remaining() const { return (u64)(_end - _at); }

		private:
			const u8* _at;
			const u8* _end;
		};

		/// @brief game.binから読んだ文字列を、エラーメッセージに入れられるようにする
		/// @details 表示できるASCII以外のバイトは\xNNにする。不正なUTF-8がそのまま--jsonの出力や
		///			 デバッガの出力(コードページで解釈される)に入らないように、ASCIIだけにする。
		std::string printable(const char* text)
		{
			std::string result{};
			for (const char* c{ text }; *c; ++c)
			{
				const u8 byte{ (u8)*c };
				if (byte >= 0x20 && byte < 0x7f && byte != '\\')
				{
					result += *c;
				}
				else
				{
					char buffer[8];
					snprintf(buffer, sizeof(buffer), "\\x%02x", byte);
					result += buffer;
				}
			}
			return result;
		}

		/// @brief Transformを読み込む
		/// @param data
		/// @param info
		/// @return 読み込みの結果
		load_game_result read_transform(blob_reader& data, game_entity::entity_info& info)
		{
			using namespace DirectX;
			f32 rotation[3];

			// すでにTransformがあるならエラー
			if (info.transform)
			{
				last_error = "duplicate transform component";
				return load_game_result::invalid_format;
			}
			if (!data.read(&transform_info.position[0], sizeof(transform_info.position)) ||	// 位置
				!data.read(&rotation[0], sizeof(rotation)) ||										// 回転
				!data.read(&transform_info.scale[0], sizeof(transform_info.scale)))				// スケール
			{
				last_error = "unexpected end of file in transform component";
				return load_game_result::invalid_format;
			}

			// 回転をオイラーからクォータニオンに変換
			XMFLOAT3A rot{ &rotation[0] };
			XMVECTOR quat{ XMQuaternionRotationRollPitchYawFromVector(XMLoadFloat3A(&rot)) };
			XMFLOAT4A rot_quat{};
			XMStoreFloat4A(&rot_quat, quat);
			memcpy(&transform_info.rotation[0], &rot_quat.x, sizeof(transform_info.rotation));

			info.transform = &transform_info;
			return load_game_result::ok;
		}

		/// @brief Scriptを読み込む
		/// @param data
		/// @param info
		/// @return 読み込みの結果
		load_game_result read_script(blob_reader& data, game_entity::entity_info& info)
		{
			// すでにScriptがあるならエラー
			if (info.script)
			{
				last_error = "duplicate script component";
				return load_game_result::invalid_format;
			}
			u32 name_length{ 0 };	// スクリプト名の長さ
			if (!data.read_u32(name_length))
			{
				last_error = "unexpected end of file in script component";
				return load_game_result::invalid_format;
			}
			// スクリプト名が255文字より長い場合、おそらくバイナリかゲームプログラマーのどちらかが非常に間違っています。
			if (!name_length || name_length > 255)
			{
				last_error = "invalid script name length " + std::to_string(name_length);
				return load_game_result::invalid_format;
			}
			char script_name[256];
			if (!data.read(&script_name[0], name_length))	// スクリプト名
			{
				last_error = "unexpected end of file in script name";
				return load_game_result::invalid_format;
			}
			// 名前をゼロ終端のc文字列にする。
			script_name[name_length] = 0;
			script_info.script_creator = script::detail::get_script_creator(script::detail::string_hash()(script_name));	// スクリプトの作成関数
			if (!script_info.script_creator)
			{
				last_error = "script '" + printable(script_name) + "' is not registered";
				return load_game_result::script_not_found;
			}
			info.script = &script_info;
			return load_game_result::ok;
		}

		// 関数ポインタの型
		using component_reader = load_game_result(*)(blob_reader&, game_entity::entity_info&);
		component_reader component_readers[]
		{
			read_transform,
			read_script
		};
		static_assert(_countof(component_readers) == conponent_type::count);	// コンポーネントの種類数と関数ポインタの数が一致していることを確認

		/// @brief ファイルを読み込む
		/// @param path ファイルのパス
		/// @param data 読み込んだデータ
		/// @param size データのサイズ
		/// @return 読み込みに成功したらtrue(空のファイルは失敗)
		bool read_file(std::filesystem::path path, std::unique_ptr<u8[]>& data, u64& size)
		{
			// ファイルが存在しない場合はfalseを返す
			std::error_code error{};
			if (!std::filesystem::is_regular_file(path, error)) return false;

			// ファイルを読み込む
			size = std::filesystem::file_size(path, error);
			if (error || !size) return false;
			data = std::make_unique<u8[]>(size);
			std::ifstream file{ path, std::ios::in | std::ios::binary };
			// ファイルが開けない場合はfalseを返す
			if (!file || !file.read((char*)data.get(), size))
			{
				file.close();
				return false;
			}

			file.close();
			return true;
		}

		/// @brief エンティティを1つ読み込んで作成する
		/// @return 読み込みの結果
		load_game_result load_entity(blob_reader& at)
		{
			// エンティティの情報を読み込む
			game_entity::entity_info info{};
			u32 entity_type{ 0 };		// エンティティ・タイプは（今のところ）読み飛ばす
			u32 num_components{ 0 };	// コンポーネント数
			if (!at.read_u32(entity_type) || !at.read_u32(num_components))
			{
				last_error = "unexpected end of file in entity header";
				return load_game_result::invalid_format;
			}
			// コンポーネント数が0ならエラー
			if (!num_components)
			{
				last_error = "entity has no components";
				return load_game_result::invalid_format;
			}

			// コンポーネントを読み込む
			for (u32 component_index{ 0 }; component_index < num_components; ++component_index)
			{
				u32 component_type{ 0 };	// コンポーネントの種類
				if (!at.read_u32(component_type))
				{
					last_error = "unexpected end of file in component type";
					return load_game_result::invalid_format;
				}
				// コンポーネントの種類が範囲外ならエラー
				if (component_type >= conponent_type::count)
				{
					last_error = "unknown component type " + std::to_string(component_type);
					return load_game_result::unknown_component;
				}
				const load_game_result result{ component_readers[component_type](at, info) };	// コンポーネントの読み込み
				if (result != load_game_result::ok) return result;
			}

			// Transformのないエンティティは作れない
			if (!info.transform)
			{
				last_error = "entity has no transform component";
				return load_game_result::invalid_format;
			}

			// エンティティを作成
			game_entity::entity entity{ game_entity::create(info) };
			if (!entity.is_valid())	// エンティティが作成できなかったらエラー
			{
				last_error = "failed to create entity";
				return load_game_result::entity_creation_failed;
			}
			entities.emplace_back(entity);	// エンティティをリストに追加
			return load_game_result::ok;
		}

		/// @brief game.binを読み込み、Entityを作成する(失敗時の後始末は呼び出し側で行う)
		/// @return 読み込みの結果
		load_game_result load_game_data(const std::filesystem::path& path)
		{
			std::error_code error{};
			const bool file_exists{ std::filesystem::exists(path, error) };
			// 存在の確認自体に失敗した(アクセスが拒否されたなど)場合は、存在しないのではなく読めないとする
			// NOTE: error.message()はOSの言語の文字列になりうるので、ASCII以外は\xNNにする
			if (error)
			{
				last_error = "failed to check the file: " + printable(error.message().c_str());
				return load_game_result::read_failed;
			}
			if (!file_exists)
			{
				last_error = "file not found";
				return load_game_result::file_not_found;
			}
			std::unique_ptr<u8[]> game_data{};
			u64 size{ 0 };
			if (!read_file(path, game_data, size))
			{
				last_error = "failed to read file (or file is empty)";
				return load_game_result::read_failed;
			}
			blob_reader at{ game_data.get(), size };
			u32 num_entities{ 0 };
			if (!at.read_u32(num_entities))
			{
				last_error = "unexpected end of file in entity count";
				return load_game_result::invalid_format;
			}
			// NOTE: エンティティが0個なら空のシーンとして成功する。
			//		 Primalはここでfalseを返すが、エディタは空のシーンも正しい形式で書き出すため受け入れる (#107)。

			// エンティティを読み込む
			for (u32 entity_index{ 0 }; entity_index < num_entities; ++entity_index)
			{
				const load_game_result result{ load_entity(at) };
				if (result != load_game_result::ok)
				{
					last_error = "entity " + std::to_string(entity_index) + ": " + last_error;
					return result;
				}
			}

			// バッファの最後まで読み込んだことを確認
			if (at.remaining())
			{
				last_error = std::to_string(at.remaining()) + " trailing bytes after the last entity";
				return load_game_result::invalid_format;
			}
			return load_game_result::ok;
		}
	}	// 匿名名前空間

	const char* to_string(load_game_result result)
	{
		switch (result)
		{
		case load_game_result::ok:						return "ok";
		case load_game_result::file_not_found:			return "file_not_found";
		case load_game_result::read_failed:				return "read_failed";
		case load_game_result::invalid_format:			return "invalid_format";
		case load_game_result::unknown_component:		return "unknown_component";
		case load_game_result::script_not_found:		return "script_not_found";
		case load_game_result::entity_creation_failed:	return "entity_creation_failed";
		}
		return "unknown";
	}

	/// @brief ゲームのBinaryファイルを読み込む
	/// @return 読み込みの結果
	load_game_result load_game(const std::filesystem::path& path)
	{
		assert(entities.empty());	// 前のゲームをunload_game()せずに読み込もうとしている
		last_error.clear();
		const load_game_result result{ load_game_data(path) };
		// 途中で失敗した場合、それまでに作成したエンティティを破棄する
		if (result != load_game_result::ok) unload_game();
		return result;
	}

	const std::string& last_load_game_error()
	{
		return last_error;
	}

	void unload_game()
	{
		// エンティティを破棄
		for (auto entity : entities)
		{
			game_entity::remove(entity.get_id());
		}
		entities.clear();
	}

	const utl::vector<game_entity::entity>& loaded_game_entities()
	{
		return entities;
	}

	bool load_engine_shaders(std::unique_ptr<u8[]>& shaders, u64& size)
	{
		auto path = graphics::get_engine_shaders_path();

		return read_file(path, shaders, size);
	}
}
#endif // !defined(SHIPPING)
