// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [FbxImporter.h]
// 作成日 : 2025/01/11
// 作成者 : 田中ミノル
// 概要 :
// FBXファイルを読み込むクラス
// 更新履歴
// 2025/01/11 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "ToolsCommon.h"
#include <fbxsdk.h>

namespace dxforge::tools
{
	struct scene_data;
	struct scene;
	struct mesh;
	struct geometry_import_settings;

	class fbx_context
	{
	public:		// パブリック関数

		/// @brief コンストラクタ
		/// @param file ファイル名
		/// @param scene シーン
		/// @param data シーンデータ
		fbx_context(const char* file, scene* scene, scene_data* data, progression* const progression)
			: _scene(scene), _scene_data(data), _progression(progression)
		{
			assert(file && _scene && _scene_data && _progression);
			if (initialize_fbx())
			{
				load_fbx_file(file);
				assert(is_valid());
			}
		}

		/// @brief デストラクタ
		~fbx_context()
		{
			_fbx_scene->Destroy();
			_fbx_manager->Destroy();
			ZeroMemory(this, sizeof(fbx_context));
		}


		void get_scene(FbxNode* root = nullptr);

		/// @brief SceneManagerとSceneが有効かどうか
		/// @return 有効ならtrue
		constexpr bool is_valid() const { return _fbx_manager && _fbx_scene; }

		/// @brief シーンスケールを取得
		constexpr f32 scene_scale() const { return _scene_scale; }

		constexpr progression* get_progression() const { return _progression; }

	private:	// プライベート関数
		bool initialize_fbx();
		void load_fbx_file(const char* file);
		void get_meshes(FbxNode* node, utl::vector<mesh>& meshes, u32 lod_id, f32 lod_threshold);
		void get_mesh(FbxNodeAttribute* attribute, utl::vector<mesh>& meshes, u32 lod_id, f32 lod_threshold);
		void get_lod_group(FbxNodeAttribute* attribute);
		bool get_mesh_data(FbxMesh* fbx_mesh, mesh& m);

	private:	// プライベート変数

		scene* _scene{ nullptr };
		scene_data* _scene_data{ nullptr };
		FbxManager* _fbx_manager{ nullptr };
		FbxScene* _fbx_scene{ nullptr };
		progression* _progression{ nullptr };
		f32 _scene_scale{ 1.0f };
	};

}	// namespace dxforge::tools


