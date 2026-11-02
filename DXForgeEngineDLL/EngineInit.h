// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [EngineInit.h]
// 作成日 : 2026/11/02
// 作成者 : 田中ミノル
// 概要
// 　エディタからエンジン(シェーダーとグラフィックス)を初期化・終了する
// 更新履歴
// 2026/11/02 新規作成(Primal Day 078 の InitializeEngine / ShutdownEngine の実体。#31)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "Graphics/Renderer.h"
#include <filesystem>

namespace dxforge::editor
{
	// NOTE: 値はエディタの EngineAPIStructs.EngineInitError と同じにする
	//       (DXForgeEditor/Tests/engine_init_test.ps1 がこのファイルを読んで確かめる)。
	struct engine_init_error
	{
		enum error_code : u32
		{
			succeeded = 0,
			unknown = 1,
			shader_compilation = 2,
			graphics = 3,
		};
	};

	/// @brief エンジンのシェーダーをコンパイルし(最新なら何もしない)、グラフィックス(Direct3D 12)を初期化する
	/// @param engine_path エンジン(リポジトリ)のルートのフォルダ。シェーダーのソースは
	///			<engine_path>\DXForgeEngine\Graphics\Direct3D12\Shaders\ から探す。空なら作業フォルダの ..\..\ をルートとみなす
	///			(EngineTest と同じ)。エディタは解決済みのエンジンのパス(MainWindow.DXForgePath)を渡す(#31 の Codex の指摘)。
	/// @details shaders.bin(.\shaders\d3d12\)は作業フォルダからの相対パス。
	///			 UIは出さない(Primal はここでシェーダーのコンパイルの再試行を MessageBox で聞くが、エディタ側で聞く)。
	///			 初期化済みなら何もせずに succeeded を返す。例外は外に出さない。
	/// @return 失敗したときは、どこで失敗したか。失敗したときは初期化していない状態に戻っている
	engine_init_error::error_code initialize_engine(const graphics::graphics_init_info& info = {}, const std::filesystem::path& engine_path = {});

	/// @brief エンジンのルートのフォルダから、エンジンのシェーダーのソースのフォルダを返す。engine_path が空なら空を返す
	std::filesystem::path engine_shaders_source_folder(const std::filesystem::path& engine_path);

	/// @brief initialize_engine() で初期化したグラフィックスを終了する。初期化していないとき、2回目は何もしない
	void shutdown_engine();

	/// @brief initialize_engine() が成功して、まだ shutdown_engine() を呼んでいないか
	bool is_engine_initialized();
}
