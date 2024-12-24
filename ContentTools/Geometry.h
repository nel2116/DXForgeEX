// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Geometry.h]
// 作成日 : 2024/12/24
// 作成者 : 田中ミノル
// 概要 :
//
// 更新履歴
// 2024/12/24 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "ToolsCommon.h"

namespace dxforge::tools
{
	// メッシュデータ
	struct mesh
	{
		// 初期データ
		utl::vector<math::v3> positions;				// 頂点座標
		utl::vector<math::v3> normals;					// 法線
		utl::vector<math::v3> tangents;					// 接線
		utl::vector< utl::vector<math::v2>> uv_sets;	// UVセット

		utl::vector<u32> raw_indices;					// インデックス
		// 中間データ

		// 出力データ
	};

	// LODグループ
	struct lod_group
	{
		std::string name;								// LODグループの名前
		utl::vector<mesh> meshes;						// メッシュ
	};

	// ゲーム中のシーンデータではなく、コンテンツファイルに存在するすべてのオブジェクトのデータ
	struct scene
	{
		std::string name;								// シーンの名前
		utl::vector<lod_group> lod_groups;				// LODグループ
	};

	// インポート設定
	struct geometry_import_settings
	{
		f32 smoothings_angle;							// スムージング角度
		u8 calculate_normals;							// 法線の計算
		u8 calculate_tangents;							// 接線の計算
		u8 reverse_handedness;							// 右手系を左手系に変換
		u8 import_embeded_textures;						// テクスチャの埋め込み
		u8 import_animations;							// アニメーションのインポート
	};

	// シーンデータ
	struct scene_data
	{
		u8* buffer;										// バッファ
		u32 buffer_size;								// バッファのサイズ
		geometry_import_settings settings;				// インポート設定
	};
}



