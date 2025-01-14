// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ContentLoader.cpp]
// 作成日 : 2024/08/20
// 作成者 : 田中ミノル
// 概要
//	コンテンツの読み込みの実装
// 更新履歴
// 2024/08/20 新規作成
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

		/// @brief Transformを読み込む
		/// @param data
		/// @param info
		/// @return 成功したらtrue
		bool read_transform(const u8*& data, game_entity::entity_info& info)
		{
			using namespace DirectX;
			f32 rotation[3];

			assert(!info.transform);	// すでにTransformがあるならエラー
			memcpy(&transform_info.position[0], data, sizeof(transform_info.position)); data += sizeof(transform_info.position);	// 位置
			memcpy(&rotation[0], data, sizeof(rotation)); data += sizeof(rotation);	// 回転
			memcpy(&transform_info.scale[0], data, sizeof(transform_info.scale)); data += sizeof(transform_info.scale);	// スケール

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
		bool read_script(const u8*& data, game_entity::entity_info& info)
		{
			assert(!info.script);	// すでにScriptがあるならエラー
			const u32 name_length{ *data }; data += sizeof(u32);	// スクリプト名の長さ
			if (!name_length) return false;	// スクリプト名の長さが0ならエラー
			// スクリプト名が255文字より長い場合、おそらくバイナリかゲームプログラマーのどちらかが非常に間違っています。
			assert(name_length < 256);	// スクリプト名が256文字より長いならエラー
			char script_name[256];
			memcpy(&script_name[0], data, name_length); data += name_length;	// スクリプト名
			// 名前をゼロ終端のc文字列にする。
			script_name[name_length] = 0;
			script_info.script_creator = script::detail::get_script_creator(script::detail::string_hash()(script_name));	// スクリプトの作成関数
			info.script = &script_info;
			return script_info.script_creator != nullptr;	// スクリプトの作成関数があるならtrue
		}

		// 関数ポインタの型
		using component_reader = bool(*)(const u8*&, game_entity::entity_info&);
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
		/// @return 読み込みに成功したらtrue
		bool read_file(std::filesystem::path path, std::unique_ptr<u8[]>& data, u64& size)
		{
			// ファイルが存在しない場合はfalseを返す
			if (!std::filesystem::exists(path)) return false;

			// ファイルを読み込む
			size = std::filesystem::file_size(path);
			assert(size);
			if (!size) return false;
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
		// データが読み込めなかったらエラー
		assert(game_data.get());
		const u8* at{ game_data.get() };
		constexpr u32 su32{ sizeof(u32) };
		const u32 num_entities{ *at }; at += su32;
		// エンティティ数が0ならエラー
		if (!num_entities) return false;

		// エンティティを読み込む
		for (u32 entity_index{ 0 }; entity_index < num_entities; ++entity_index)
		{
			// エンティティの情報を読み込む
			game_entity::entity_info info{};
			const u32 entity_type{ *at }; at += su32;		// エンティティの種類
			const u32 num_components{ *at }; at += su32;	// コンポーネント数
			if (!num_components) return false;				// コンポーネント数が0ならエラー

			// コンポーネントを読み込む
			for (u32 component_index{ 0 }; component_index < num_components; ++component_index)
			{
				const u32 component_type{ *at }; at += su32;	// コンポーネントの種類
				assert(component_type < conponent_type::count);	// コンポーネントの種類が範囲外ならエラー
				if (!component_readers[component_type](at, info)) return false;	// コンポーネントの読み込み
			}

			// エンティティを作成
			assert(info.transform);
			game_entity::entity entity{ game_entity::create(info) };	// エンティティを作成
			if (!entity.is_valid()) return false;	// エンティティが作成できなかったらエラー
			entities.emplace_back(entity);	// エンティティをリストに追加
		}

		assert(at == game_data.get() + size);	// バッファの最後まで読み込んだことを確認
		return true;
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