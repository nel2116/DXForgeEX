// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ContentLoader.cpp]
// 作成日 : 2024/08/20
// 作成者 : 田中ミノル
// 概要
//	コンテンツの読み込みの実装
// 更新履歴
// 2024/08/20 新規作成
// 2026/10/27 u32の値を1バイトで読んでいたのを直し、壊れたgame.binを範囲外読み・assertではなくエラーとして扱うようにした (#112)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "ContentLoader.h"
#include "Components/Entity.h"
#include "Components/Transform.h"
#include "Components/Script.h"
#include "Graphics/Renderer.h"

#if !defined(SHIPPING) && defined(_WIN64)
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

		/// @brief Transformを読み込む
		/// @param data
		/// @param info
		/// @return 成功したらtrue
		bool read_transform(blob_reader& data, game_entity::entity_info& info)
		{
			using namespace DirectX;
			f32 rotation[3];

			if (info.transform) return false;	// すでにTransformがあるならエラー
			if (!data.read(&transform_info.position[0], sizeof(transform_info.position)) ||	// 位置
				!data.read(&rotation[0], sizeof(rotation)) ||										// 回転
				!data.read(&transform_info.scale[0], sizeof(transform_info.scale)))				// スケール
			{
				return false;
			}

			// 回転をオイラーからクォータニオンに変換
			XMFLOAT3A rot{ &rotation[0] };
			XMVECTOR quat{ XMQuaternionRotationRollPitchYawFromVector(XMLoadFloat3A(&rot)) };
			XMFLOAT4A rot_quat{};
			XMStoreFloat4A(&rot_quat, quat);
			memcpy(&transform_info.rotation[0], &rot_quat.x, sizeof(transform_info.rotation));

			info.transform = &transform_info;
			return true;
		}

		/// @brief Scriptを読み込む
		/// @param data
		/// @param info
		/// @return 成功したらtrue
		bool read_script(blob_reader& data, game_entity::entity_info& info)
		{
			if (info.script) return false;	// すでにScriptがあるならエラー
			u32 name_length{ 0 };	// スクリプト名の長さ
			if (!data.read_u32(name_length)) return false;
			// スクリプト名が255文字より長い場合、おそらくバイナリかゲームプログラマーのどちらかが非常に間違っています。
			if (!name_length || name_length > 255) return false;
			char script_name[256];
			if (!data.read(&script_name[0], name_length)) return false;	// スクリプト名
			// 名前をゼロ終端のc文字列にする。
			script_name[name_length] = 0;
			script_info.script_creator = script::detail::get_script_creator(script::detail::string_hash()(script_name));	// スクリプトの作成関数
			info.script = &script_info;
			return script_info.script_creator != nullptr;	// スクリプトの作成関数があるならtrue
		}

		// 関数ポインタの型
		using component_reader = bool(*)(blob_reader&, game_entity::entity_info&);
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
	}	// 匿名名前空間

	/// @brief ゲームのBinaryファイルを読み込む
	/// @return 読み込みに成功したらtrue
	bool load_game()
	{
		// game.binを読み込み、Entityを作成する。
		std::unique_ptr<u8[]> game_data{};
		u64 size{ 0 };
		if (!read_file("game.bin", game_data, size)) return false;
		blob_reader at{ game_data.get(), size };
		u32 num_entities{ 0 };
		if (!at.read_u32(num_entities)) return false;
		// エンティティ数が0ならエラー
		if (!num_entities) return false;

		// エンティティを読み込む
		for (u32 entity_index{ 0 }; entity_index < num_entities; ++entity_index)
		{
			// エンティティの情報を読み込む
			game_entity::entity_info info{};
			u32 entity_type{ 0 };		// エンティティ・タイプは（今のところ）読み飛ばす
			u32 num_components{ 0 };	// コンポーネント数
			if (!at.read_u32(entity_type) || !at.read_u32(num_components)) return false;
			if (!num_components) return false;				// コンポーネント数が0ならエラー

			// コンポーネントを読み込む
			for (u32 component_index{ 0 }; component_index < num_components; ++component_index)
			{
				u32 component_type{ 0 };	// コンポーネントの種類
				if (!at.read_u32(component_type)) return false;
				if (component_type >= conponent_type::count) return false;	// コンポーネントの種類が範囲外ならエラー
				if (!component_readers[component_type](at, info)) return false;	// コンポーネントの読み込み
			}

			// エンティティを作成
			if (!info.transform) return false;	// Transformのないエンティティは作れない
			game_entity::entity entity{ game_entity::create(info) };	// エンティティを作成
			if (!entity.is_valid()) return false;	// エンティティが作成できなかったらエラー
			entities.emplace_back(entity);	// エンティティをリストに追加
		}

		// バッファの最後まで読み込んだことを確認
		return !at.remaining();
	}

	void unload_game()
	{
		// エンティティを破棄
		for (auto entity : entities)
		{
			game_entity::remove(entity.get_id());
		}
	}

	bool load_engine_shaders(std::unique_ptr<u8[]>& shaders, u64& size)
	{
		auto path = graphics::get_engine_shaders_path();

		return read_file(path, shaders, size);
	}
}
#endif // !defined(SHIPPING)
