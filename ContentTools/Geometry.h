// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [Geometry.h]
// 作成日 : 2024/12/24
// 作成者 : 田中ミノル
// 概要 :
//
// 更新履歴
// 2024/12/24 新規作成
// 2025/01/12 elements_typeの追加
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "ToolsCommon.h"

namespace dxforge::tools
{
	// 頂点データ
	struct vertex
	{
		math::v4 tangent{};
		math::v4 joint_weights{};
		math::u32v4 joint_indices{ u32_invalid_id,u32_invalid_id, u32_invalid_id, u32_invalid_id };
		math::v3 position{};
		math::v3 normal{};
		math::v2 uv{};
		u8 red{}, green{}, blue{};
		u8 pad{};
	};

	namespace elements
	{

		struct elements_type
		{
			enum type : u32
			{
				position_only = 0x00,
				static_normal = 0x01,
				static_normal_texture = 0x03,
				static_color = 0x04,
				skeletal = 0x08,
				skeletal_color = skeletal | static_color,
				skeletal_normal = skeletal | static_normal,
				skeletal_normal_color = skeletal_normal | static_color,
				skeletal_normal_texture = skeletal | static_normal_texture,
				skeletal_normal_texture_color = skeletal_normal_texture | static_color,
			};
		};

		struct static_color
		{
			u8 color[3];
			u8 pad;
		};

		struct static_normal
		{
			u8 color[3];
			u8 t_sign;		// bit 0: tangent handedness, bit 1: tangent.z sign, bit 2: normal.z sign (0 means -1, 1 means +1).
			u16 normal[2];
		};

		struct static_normal_texture
		{
			u8 color[3];
			u8 t_sign;		// bit 0: tangent handedness, bit 1: tangent.z sign, bit 2: normal.z sign (0 means -1, 1 means +1).
			u16 normal[2];
			u16 tangent[2];
			math::v2 uv;
		};

		struct skeletal
		{
			u8 joint_weights[3];	// normalized joint weights for up 4 joints
			u8 pad;
			u16 joint_indices[4];
		};

		struct skeletal_color
		{
			u8 joint_weights[3];	// normalized joint weights for up 4 joints
			u8 pad;
			u16 joint_indices[4];
			u8 color[3];
			u8 pad2;
		};

		struct skeletal_normal
		{
			u8 joint_weights[3];	// normalized joint weights for up 4 joints
			u8 t_sign;				// bit 0: tangent handedness, bit 1: tangent.z sign, bit 2: normal.z sign (0 means -1, 1 means +1).
			u16 joint_indices[4];
			u16 normal[2];
		};

		struct skeletal_normal_color
		{
			u8 joint_weights[3];	// normalized joint weights for up 4 joints
			u8 t_sign;				// bit 0: tangent handedness, bit 1: tangent.z sign, bit 2: normal.z sign (0 means -1, 1 means +1).
			u16 joint_indices[4];
			u16 normal[2];
			u8 color[3];
			u8 pad;
		};

		struct skeletal_normal_texture
		{
			u8 joint_weights[3];	// normalized joint weights for up 4 joints
			u8 t_sign;				// bit 0: tangent handedness, bit 1: tangent.z sign, bit 2: normal.z sign (0 means -1, 1 means +1).
			u16 joint_indices[4];
			u16 normal[2];
			u16 tangent[2];
			math::v2 uv;
		};

		struct skeletal_normal_texture_color
		{
			u8 joint_weights[3];	// normalized joint weights for up 4 joints
			u8 t_sign;				// bit 0: tangent handedness, bit 1: tangent.z sign, bit 2: normal.z sign (0 means -1, 1 means +1).
			u16 joint_indices[4];
			u16 normal[2];
			u16 tangent[2];
			math::v2 uv;
			u8 color[3];
			u8 pad;
		};

	}	// namespace elements

	// メッシュデータ
	struct mesh
	{
		// 初期データ
		utl::vector<math::v3> positions;				// 頂点座標
		utl::vector<math::v3> normals;					// 法線
		utl::vector<math::v4> tangents;					// 接線
		utl::vector<math::v3> colors;					// カラー
		utl::vector< utl::vector<math::v2>> uv_sets;	// UVセット
		utl::vector<u32> material_indices;				// マテリアルインデックス
		utl::vector<u32> material_used;					// マテリアルが使用されているかどうか

		utl::vector<u32> raw_indices;					// インデックス

		// 中間データ
		utl::vector<vertex> vertices;					// 頂点
		utl::vector<u32> indices;						// インデックス

		// 出力データ
		std::string name;								// メッシュの名前
		elements::elements_type::type elements_type;	// 要素タイプ
		utl::vector<u8> position_buffer;				// 頂点座標バッファ
		utl::vector<u8> element_buffer;					// 要素バッファ
		f32 lod_threshold{ -1.0f }; 					// LOD閾値
		u32 lod_id{ u32_invalid_id };					// LOD ID
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
		f32 smoothing_angle;							// スムージング角度
		u8 calculate_normals;							// 法線の計算
		u8 calculate_tangents;							// 接線の計算
		u8 reverse_handedness;							// 右手系を左手系に変換
		u8 import_embedded_textures;					// テクスチャの埋め込み
		u8 import_animations;							// アニメーションのインポート
		u8 coalesce_meshes;								// メッシュの結合
	};

	// シーンデータ
	struct scene_data
	{
		u8* buffer;										// バッファ
		u32 buffer_size;								// バッファのサイズ
		geometry_import_settings settings;				// インポート設定
	};

	void process_scene(scene& scene, const geometry_import_settings& settings, progression* const progression);
	void pack_data(const scene& scene, scene_data& data);
	bool coalesce_meshes(const lod_group& lod, mesh& combined_mesh, progression* const progression);
} // namespace dxforge::tools



