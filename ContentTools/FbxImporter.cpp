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

	/// @brief FBX SDKの初期化
	/// @return 初期化に成功したかどうか
	bool fbx_context::initialize_fbx()
	{
		assert(!is_valid());

		_fbx_manager = FbxManager::Create();
		if (!_fbx_manager)
		{
			return false;
		}

		FbxIOSettings* ios{ FbxIOSettings::Create(_fbx_manager, IOSROOT) };
		assert(ios);
		_fbx_manager->SetIOSettings(ios);

		return true;
	}

	/// @brief FBXファイルの読み込み
	/// @param file ファイル名
	void fbx_context::load_fbx_file(const char* file)
	{
		assert(_fbx_manager && !_fbx_scene);
		_fbx_scene = FbxScene::Create(_fbx_manager, "Importer Scene");
		if (!_fbx_scene)
		{
			return;
		}

		FbxImporter* importer{ FbxImporter::Create(_fbx_manager, "Importer") };
		if (!(importer &&
			importer->Initialize(file, -1, _fbx_manager->GetIOSettings()) &&
			importer->Import(_fbx_scene)))
		{
			return;
		}

		importer->Destroy();

		// シーンのスケールをメートル単位で取得する。
		_scene_scale = (f32)_fbx_scene->GetGlobalSettings().GetSystemUnit().GetConversionFactorTo(FbxSystemUnit::m);
	}

	/// @brief シーンの取得
	/// @param root ルートノード
	void fbx_context::get_scene(FbxNode* root /*= nullptr*/)
	{
		assert(is_valid());

		if (!root)
		{
			root = _fbx_scene->GetRootNode();
			if (!root) return;
		}

		const s32 num_nodes{ root->GetChildCount() };

		if (_scene_data->settings.coalesce_meshes)
		{
			lod_group lod{};
			for (s32 i{ 0 }; i < num_nodes; ++i)
			{
				FbxNode* node{ root->GetChild(i) };
				if (!node) continue;

				get_meshes(node, lod.meshes, 0, -1.f);
			}

			if (lod.meshes.size())
			{
				lod.name = lod.meshes[0].name;
				mesh combined_mesh{};

				if (coalesce_meshes(lod, combined_mesh, _progression))
				{
					lod.meshes.clear();
					lod.meshes.emplace_back(combined_mesh);
				}
				_scene->lod_groups.emplace_back(lod);
			}
		}
		else
		{
			for (s32 i{ 0 }; i < num_nodes; ++i)
			{
				FbxNode* node{ root->GetChild(i) };
				if (!node) continue;

				lod_group lod{};
				get_meshes(node, lod.meshes, 0, -1.f);
				if (lod.meshes.size())
				{
					lod.name = lod.meshes[0].name;
					_scene->lod_groups.emplace_back(lod);
				}
			}
		}
	}

	/// @brief メッシュの取得
	/// @param node ノード
	/// @param meshes メッシュリスト
	/// @param lod_id LOD ID
	/// @param lod_threshold LOD閾値
	void fbx_context::get_meshes(FbxNode* node, utl::vector<mesh>& meshes, u32 lod_id, f32 lod_threshold)
	{
		assert(node && lod_id != u32_invalid_id);
		bool is_lod_group{ false };

		if (const s32 num_attributes{ node->GetNodeAttributeCount() })
		{
			for (s32 i{ 0 }; i < num_attributes; ++i)
			{
				FbxNodeAttribute* attribute{ node->GetNodeAttributeByIndex(i) };
				const FbxNodeAttribute::EType attribute_type{ attribute->GetAttributeType() };
				if (attribute_type == FbxNodeAttribute::eMesh)
				{
					get_mesh(attribute, meshes, lod_id, lod_threshold);
				}
				else if (attribute_type == FbxNodeAttribute::eLODGroup)
				{
					get_lod_group(attribute);
					is_lod_group = true;
				}
			}
		}

		if (!is_lod_group)
		{
			if (const s32 num_children{ node->GetChildCount() })
			{
				for (s32 i{ 0 }; i < num_children; ++i)
				{
					get_meshes(node->GetChild(i), meshes, lod_id, lod_threshold);
				}
			}
		}
	}

	/// @brief メッシュの取得
	/// @param attribute ノード属性
	/// @param meshes メッシュリスト
	/// @param lod_id LOD ID
	/// @param lod_threshold LOD閾値
	void fbx_context::get_mesh(FbxNodeAttribute* attribute, utl::vector<mesh>& meshes, u32 lod_id, f32 lod_threshold)
	{
		assert(attribute);

		FbxMesh* fbx_mesh{ (FbxMesh*)attribute };
		if (fbx_mesh->RemoveBadPolygons() < 0) return;

		// 必要であればメッシュを三角形化する。
		FbxGeometryConverter gc{ _fbx_manager };
		fbx_mesh = (FbxMesh*)gc.Triangulate(fbx_mesh, true);
		if (!fbx_mesh || fbx_mesh->RemoveBadPolygons() < 0) return;

		FbxNode* const node{ fbx_mesh->GetNode() };

		mesh m;
		m.lod_id = lod_id;
		m.lod_threshold = lod_threshold;
		m.name = (node->GetName()[0] != '\0') ? node->GetName() : fbx_mesh->GetName();

		if (get_mesh_data(fbx_mesh, m))
		{
			meshes.emplace_back(m);
			_progression->callback(_progression->value(), _progression->max_value() + 1);
		}
	}

	/// @brief LODグループの取得
	/// @param attribute ノード属性
	void fbx_context::get_lod_group(FbxNodeAttribute* attribute)
	{
		assert(attribute);

		FbxLODGroup* lod_grp{ (FbxLODGroup*)attribute };
		FbxNode* const node{ lod_grp->GetNode() };
		lod_group lod{};
		lod.name = (node->GetName()[0] != '\0') ? node->GetName() : lod_grp->GetName();
		// NOTE: LODの数はベースメッシュ（LOD 0）専用です。
		const s32 num_nodes{ node->GetChildCount() };
		assert(num_nodes > 0 && lod_grp->GetNumThresholds() == (num_nodes - 1));

		for (s32 i{ 0 }; i < num_nodes; ++i)
		{
			f32 lod_threshold{ -1.f };
			if (i > 0)
			{
				FbxDistance threshold;
				lod_grp->GetThreshold(i - 1, threshold);
				lod_threshold = threshold.value() * _scene_scale;
			}

			get_meshes(node->GetChild(i), lod.meshes, (u32)lod.meshes.size(), lod_threshold);
		}

		if (lod.meshes.size()) _scene->lod_groups.emplace_back(lod);
	}

	/// @brief メッシュデータの取得
	/// @param fbx_mesh FBXメッシュ
	/// @param m メッシュデータ
	/// @return 成功したかどうか
	bool fbx_context::get_mesh_data(FbxMesh* fbx_mesh, mesh& m)
	{
		assert(fbx_mesh);

		FbxNode* const node{ fbx_mesh->GetNode() };
		FbxAMatrix geometricTransform;

		geometricTransform.SetT(node->GetGeometricTranslation(FbxNode::eSourcePivot));
		geometricTransform.SetR(node->GetGeometricRotation(FbxNode::eSourcePivot));
		geometricTransform.SetS(node->GetGeometricScaling(FbxNode::eSourcePivot));

		FbxAMatrix transform{ node->EvaluateGlobalTransform() * geometricTransform };
		FbxAMatrix inverse_transpose{ transform.Inverse().Transpose() };

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

			// 頂点参照が存在する場合は、そのインデックスを使用します。
			if (vertex_ref[v_idx] != u32_invalid_id)
			{
				m.raw_indices[i] = vertex_ref[v_idx];
			}
			else
			{
				FbxVector4 v = transform.MultT(vertices[v_idx]) * _scene_scale;
				m.raw_indices[i] = (u32)m.positions.size();
				vertex_ref[v_idx] = m.raw_indices[i];
				m.positions.emplace_back((f32)v[0], (f32)v[1], (f32)v[2]);
			}
		}

		assert(m.raw_indices.size() % 3 == 0);

		// ポリゴンごとの材料インデックスを取得
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
			if (fbx_mesh->GenerateNormals() &&
				fbx_mesh->GetPolygonVertexNormals(normals) && normals.Size() > 0)
			{
				const s32 num_normals{ normals.Size() };
				for (s32 i{ 0 }; i < num_normals; ++i)
				{
					FbxVector4 n{ inverse_transpose.MultT(normals[i]) };
					n.Normalize();
					m.normals.emplace_back((f32)n[0], (f32)n[1], (f32)n[2]);
				}
			}
			else
			{
				// FBXから法線をインポートする際に何か問題が発生した。
				// 独自の法線計算方法に戻る。
				_scene_data->settings.calculate_normals = true;
			}
		}

		// タンジェントをインポートする
		if (import_tangents)
		{
			FbxLayerElementArrayTemplate<FbxVector4>* tangents{ nullptr };
			// FBXの組み込みメソッドを使用してタンジェントを計算しますが、タンジェントデータがすでに存在しない場合に限ります。
			// NOTE: ドキュメントによると、この関数は接線データがすでに存在し、pOverwrite == falseの場合にtrueを返す。
			//		これは間違っている（つまり、その場合はfalseを返す）！
			fbx_mesh->GenerateTangentsData();

			if (fbx_mesh->GetTangents(&tangents) && tangents &&
				tangents->GetCount() == m.raw_indices.size())
			{
				const s32 num_tangent{ tangents->GetCount() };
				for (s32 i{ 0 }; i < num_tangent; ++i)
				{
					// TODO: この変換が正しいかどうかはわからない。
					FbxVector4 t{ tangents->GetAt(i) };
					const f32 handedness{ (f32)t[3] };
					t[3] = 0.0;
					t = transform.MultT(t);
					t.Normalize();
					m.tangents.emplace_back((f32)t[0], (f32)t[1], (f32)t[2], -handedness);
				}
			}
			else
			{
				// FBXからタンジェントをインポートするときに何か問題が発生した。
				// 独自のタンジェント計算方法に戻る。
				_scene_data->settings.calculate_tangents = true;
			}
		}

		// UVを取得する
		FbxStringList uv_names;
		fbx_mesh->GetUVSetNames(uv_names);
		const s32 uv_set_count{ uv_names.GetCount() };
		// NOTE: UVセットがなくても大丈夫だ。 例えば、発光するオブジェクトの中には、uvマップを必要としないものがある。
		m.uv_sets.resize(uv_set_count);

		for (s32 i{ 0 }; i < uv_set_count; ++i)
		{
			FbxArray<FbxVector2> uvs;
			if (fbx_mesh->GetPolygonVertexUVs(uv_names.GetStringAt(i), uvs))
			{
				const s32 num_uvs{ uvs.Size() };
				for (s32 j{ 0 }; j < num_uvs; ++j)
				{
					// FBX UVの原点が常に左下にあると仮定すると、DirectXは左上隅を原点として使用するため、
					// V軸は反転するはずです。
					m.uv_sets[i].emplace_back((f32)uvs[j][0], 1.f - (f32)uvs[j][1]);
				}
			}
		}

		return true;
	}

	/// @brief fbxモデルを読み込む
	/// @param file FBXファイル名
	/// @param data シーンデータ
	/// @param callback 進捗コールバック
	/// @return 成功した場合はtrue
	EDITOR_INTERFACE void ImportFbx(const char* file, scene_data* data, progression::progress_callback callback)
	{
		assert(file && data);
		scene scene{};
		progression progression{ callback };

		// NOTE: FBX SDKを使用するものは、シングルスレッドでなければならない。
		{
			std::lock_guard lock{ fbx_mutex };
			fbx_context fbx_context{ file, &scene, data, &progression };
			if (fbx_context.is_valid())
			{
				fbx_context.get_scene();
			}
		}

		if (scene.lod_groups.empty())
		{
			// TODO: 失敗ログメッセージをエディターに送る
			return;
		}

		process_scene(scene, data->settings, &progression);
		pack_data(scene, *data);
	}
}	// namespace dxforge::tools