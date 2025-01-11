// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [FbxImporter.cpp]
// 作成日 : 2025/01/11
// 作成者 : 田中ミノル
// 概要 :
// FBXファイルを読み込むクラス
// 更新履歴
// 2025/01/11 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// ====== インクルード部 ======
#include "FbxImporter.h"
#include "Geometry.h"


// ====== リンク部 ======
// コンパイルエラーやリンカーエラーが発生した場合は、次のことを確認してください。
// 1) FBX SDK 2020.3.7以降がインストールされていること。
// 2) fbxsdk.hへのインクルードパスが「追加インクルードディレクトリ」（コンパイラの設定）に追加されていること。
// 3) 次のセクションのライブラリ・パスは、正しい場所を指していること。
#if _DEBUG
#pragma comment(lib,"C:\\Program Files\\Autodesk\\FBX\\FBX SDK\\2020.3.7\\lib\\x64\\debug\\libfbxsdk-md.lib")
#pragma comment(lib,"C:\\Program Files\\Autodesk\\FBX\\FBX SDK\\2020.3.7\\lib\\x64\\debug\\libxml2-md.lib")
#pragma comment(lib,"C:\\Program Files\\Autodesk\\FBX\\FBX SDK\\2020.3.7\\lib\\x64\\debug\\zlib-md.lib")
#else
#pragma comment(lib,"C:\\Program Files\\Autodesk\\FBX\\FBX SDK\\2020.3.7\\lib\\x64\\release\\libfbxsdk-md.lib")
#pragma comment(lib,"C:\\Program Files\\Autodesk\\FBX\\FBX SDK\\2020.3.7\\lib\\x64\\release\\libxml2-md.lib")
#pragma comment(lib,"C:\\Program Files\\Autodesk\\FBX\\FBX SDK\\2020.3.7\\lib\\x64\\release\\zlib-md.lib")
#endif
// LNK4099 PDB not found警告は、FBX SDK PDBをインストール（別途ダウンロード）するか、リンカーオプションでこの警告を無効にする（リンカーコマンドライン：/ignore:4099）ことで解決できます。

// ====== 名前空間 ======
namespace dxforge::tools
{
	namespace
	{
		std::mutex fbx_mutex{};

	}	// 匿名名前空間
	// ====== メンバ関数 ======

	// ファイル読み込み
	bool fbx_context::initialize_fbx()
	{
		assert(!is_valid());

		_fbx_manager = FbxManager::Create();
		if (!_fbx_manager)
		{
			return false;
		}

		FbxIOSettings* ios = FbxIOSettings::Create(_fbx_manager, IOSROOT);
		assert(ios);
		_fbx_manager->SetIOSettings(ios);

		return true;
	}

	// FBXファイル読み込み
	void fbx_context::load_fbx_file(const char* file)
	{
		assert(_fbx_manager && !_fbx_scene);
		_fbx_scene = FbxScene::Create(_fbx_manager, "Importer scene");
		if (!_fbx_scene)
		{
			return;
		}

		FbxImporter* importer{ FbxImporter::Create(_fbx_manager, "Importer") };
		if (!(importer && importer->Initialize(file, -1, _fbx_manager->GetIOSettings()) && importer->Import(_fbx_scene)))
		{
			return;
		}

		importer->Destroy();

		// シーンスケールをメートル単位で取得
		_scene_scale = static_cast<f32>(_fbx_scene->GetGlobalSettings().GetSystemUnit().GetConversionFactorTo(FbxSystemUnit::m));
	}

	void fbx_context::get_scene(FbxNode* root)
	{
		assert(is_valid());

		if (!root)
		{
			root = _fbx_scene->GetRootNode();
			if (!root) return;
		}

		const s32 num_nodes{ root->GetChildCount() };
		for (s32 i{ 0 }; i < num_nodes; ++i)
		{
			FbxNode* node{ root->GetChild(i) };
			if (!node) continue;

			if (node->GetMesh())
			{
				lod_group lod{};
				get_mesh(node, lod.meshes);
				if (lod.meshes.size())
				{
					lod.name = lod.meshes[0].name;
					_scene->lod_groups.emplace_back(lod);
				}
			}
			else if (node->GetLodGroup())
			{
				get_lod_group(node);
			}
			else
			{
				// さらに下の階層にメッシュがあるかどうかを確認する。
				get_scene(node);
			}
		}
	}

	void fbx_context::get_mesh(FbxNode* node, utl::vector<mesh>& meshes)
	{
		assert(node);
		if (FbxMesh * fbx_mesh{ node->GetMesh() })
		{
			if (fbx_mesh->RemoveBadPolygons() > 0) return;

			// 必要に応じてメッシュを三角形にする
			FbxGeometryConverter gc{ _fbx_manager };
			fbx_mesh = static_cast<FbxMesh*>(gc.Triangulate(fbx_mesh, true));
			if (!fbx_mesh || fbx_mesh->RemoveBadPolygons() < 0) return;

			mesh m;
			m.lod_id = static_cast<u32>(meshes.size());
			m.lod_threshold = -1.0f;
			m.name = (node->GetName()[0] != '\0') ? node->GetName() : fbx_mesh->GetName();

			if (get_mesh_data(fbx_mesh, m))
			{
				meshes.emplace_back(m);
			}
		}

		// さらに下の階層にメッシュがあるかどうかを確認する。
		get_scene(node);
	}


	void fbx_context::get_lod_group(FbxNode* node)
	{
		assert(node);

		if (FbxLODGroup * lod_grp{ node->GetLodGroup() })
		{
			lod_group lod{};
			lod.name = (node->GetName()[0] != '\0') ? node->GetName() : lod_grp->GetName();
			// NOTE: LODの数はベースメッシュ( LOD 0)に限定されます。
			const s32 num_lods{ lod_grp->GetNumThresholds() };
			const s32 num_nodes{ node->GetChildCount() };
			assert(num_lods > 0 && num_nodes > 0);

			for (s32 i{ 0 }; i < num_nodes; ++i)
			{
				get_mesh(node->GetChild(i), lod.meshes);

				if (lod.meshes.size() > 1 && lod.meshes.size() <= num_lods + 1 && lod.meshes.back().lod_threshold < 0.0f)
				{
					FbxDistance threshold;
					lod_grp->GetThreshold((u32)lod.meshes.size() - 2, threshold);
					lod.meshes.back().lod_threshold = threshold.value() * _scene_scale;
				}
			}
			if (lod.meshes.size()) _scene->lod_groups.emplace_back(lod);
		}
	}

	bool fbx_context::get_mesh_data(FbxMesh* fbx_mesh, mesh& m)
	{
		assert(fbx_mesh);
		const s32 num_polys{ fbx_mesh->GetPolygonCount() };
		if (num_polys <= 0) return false;

		// 頂点の取得
		const s32 num_vertices{ fbx_mesh->GetControlPointsCount() };
		FbxVector4* vertices{ fbx_mesh->GetControlPoints() };
		const s32 num_indices{ fbx_mesh->GetPolygonVertexCount() };
		s32* indices{ fbx_mesh->GetPolygonVertices() };

		assert(num_vertices > 0 && vertices && num_indices > 0 && indices);
		if (!(num_vertices > 0 && vertices && num_indices > 0 && indices)) return false;

		m.raw_indices.resize(num_indices);
		utl::vector vertex_ref(num_vertices, u32_invalid_id);

		for (s32 i{ 0 }; i < num_indices; ++i)
		{
			const u32 v_idx{ (u32)indices[i] };
			// 以前にこの頂点に出会ったことがあっただろうか？ もしそうなら、そのインデックスを追加してください。
			// そうでなければ、頂点と新しいインデックスを追加する。
			if (vertex_ref[v_idx] != u32_invalid_id)
			{
				m.raw_indices[i] = vertex_ref[v_idx];
			}
			else
			{
				FbxVector4 v = vertices[v_idx] * _scene_scale;
				m.raw_indices[i] = (u32)m.positions.size();
				vertex_ref[v_idx] = m.raw_indices[i];
				m.positions.emplace_back((f32)v[0], (f32)v[1], (f32)v[2]);
			}
		}
		assert(m.raw_indices.size() % 3 == 0);

		// ポリゴンごとのマテリアルインデックスを取得
		assert(num_polys > 0);
		FbxLayerElementArrayTemplate<s32>* mtl_indices;
		if (fbx_mesh->GetMaterialIndices(&mtl_indices))
		{
			for (s32 i{ 0 }; i < num_polys; ++i)
			{
				const s32 mtl_index{ mtl_indices->GetAt(i) };
				assert(mtl_index >= 0);
				m.material_indices.emplace_back((u32)mtl_index);
				if (std::find(m.material_used.begin(), m.material_used.end(), (u32)mtl_index) == m.material_used.end())
				{
					m.material_used.emplace_back((u32)mtl_index);
				}
			}
		}

		// 法線のインポートはデフォルトでオン
		const bool import_normals{ !_scene_data->settings.calculate_normals };
		// 接線のインポートはデフォルトでオフ
		const bool import_tangents{ !_scene_data->settings.calculate_tangents };

		// 法線のインポート
		if (import_normals)
		{
			FbxArray<FbxVector4> normals;
			// FBXの組み込みメソッドを使用して法線を計算しますが、法線データがすでに存在しない場合に限ります。
			if (fbx_mesh->GenerateNormals() && fbx_mesh->GetPolygonVertexNormals(normals) && normals.Size() > 0)
			{
				const s32 num_normals{ normals.Size() };
				for (s32 i{ 0 }; i < num_normals; ++i)
				{
					m.normals.emplace_back((f32)normals[i][0], (f32)normals[i][1], (f32)normals[i][2]);
				}
			}
			else
			{
				// FBXから法線をインポートする際に何か問題が発生した。
				// 通常の計算方法に戻る。
				_scene_data->settings.calculate_normals = true;
			}
		}
		// タンジェントのインポート
		if (import_tangents)
		{
			FbxLayerElementArrayTemplate<FbxVector4>* tangents{ nullptr };
			// FBXの組み込みメソッドを使用してタンジェントを計算しますが、タンジェントデータがすでに存在しない場合に限ります。
			if (fbx_mesh->GenerateTangentsData() && fbx_mesh->GetTangents(&tangents) && tangents && tangents->GetCount() > 0)
			{
				const s32 num_tangent{ tangents->GetCount() };
				for (s32 i{ 0 }; i < num_tangent; ++i)
				{
					FbxVector4 t{ tangents->GetAt(i) };
					m.tangents.emplace_back((f32)t[0], (f32)t[1], (f32)t[2], (f32)t[3]);
				}
			}
			else
			{
				// FBXからタンジェントをインポートするときに何か問題が発生した。
				// 我々独自のタンジェント計算法に戻る。
				_scene_data->settings.calculate_tangents = true;
			}
		}

		// UVを取得する
		FbxStringList uv_names;
		fbx_mesh->GetUVSetNames(uv_names);
		const s32 uv_set_count{ uv_names.GetCount() };
		// NOTE: UVセットがなくても大丈夫。 例えば、発光するオブジェクトの中には、uvマップを必要としないものがある。
		m.uv_sets.resize(uv_set_count);
		for (s32 i{ 0 }; i < uv_set_count; ++i)
		{
			FbxArray<FbxVector2> uvs;
			if (fbx_mesh->GetPolygonVertexUVs(uv_names.GetStringAt(i), uvs))
			{
				const s32 num_uvs{ uvs.Size() };
				for (s32 j{ 0 }; j < num_uvs; ++j)
				{
					m.uv_sets[i].emplace_back((f32)uvs[j][0], (f32)uvs[j][1]);
				}
			}
		}
		return true;
	}

	// ====== グローバル関数 ======
	EDITOR_INTERFACE void ImportFbx(const char* file, scene_data* data)
	{
		assert(file && data);
		scene scene{};

		// NOTE: SDKを使用するものはシングルスレッドであるべきです。
		{
			std::lock_guard lock{ fbx_mutex };
			fbx_context fbx_context{ file, &scene, data };
			if (fbx_context.is_valid())
			{
				fbx_context.get_scene();
			}
			else
			{
				// TODO: send failure message to editor
				return;
			}
		}

		process_scene(scene, data->settings);
		pack_data(scene, *data);
	}
}	// namespace dxforge::tools