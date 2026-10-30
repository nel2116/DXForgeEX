// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [ResourceUpload.h]
// 作成日 : 2026/10/30
// 作成者 : 田中ミノル
// 概要
// 　エディタから渡されたアセット(ジオメトリ・テクスチャ・マテリアル)のデータからエンジンのリソースを作る
// 更新履歴
// 2026/10/30 新規作成(Primal Day 077 の CreateResource / DestroyResource と、Day 078 のその実体。#30)
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "CommonHeaders.h"
#include "Graphics/Renderer.h"
#include "Content/ContentToEngine.h"

namespace dxforge::editor
{
	// エディタのマテリアルのデータ(AppliedMaterial.PackForEngine())の並び:
	// struct {
	//  u32                 texture_count;
	//  id::id_type         texture_ids[texture_count];
	//  struct {                                        // = graphics::material_init_info
	//   id::id_type*        texture_ids;               // エディタは0を書く。ここで texture_ids[] を指すようにする
	//   material_surface    surface;
	//   material_type::type type;
	//   u32                 texture_count;
	//   id::id_type         shader_ids[shader_type::count];
	//  } info;
	// }
	// NOTE: エディタは material_init_info をアラインメントなしで詰めて書くので、ここの大きさとオフセットを変えたら
	//       エディタの AppliedMaterial.PackForEngine() も合わせる(EngineUnitTests の Tests_ResourceUpload.cpp で固定している)。
	constexpr u32 packed_material_init_info_size{ 88 };

	/// @brief エディタのマテリアルのデータの大きさ
	constexpr u32 packed_material_size(u32 texture_count)
	{
		return (u32)sizeof(u32) + texture_count * (u32)sizeof(id::id_type) + packed_material_init_info_size;
	}

	/// @brief エディタのマテリアルのデータを material_init_info に直す(Primal の patch_material_data)
	/// @param data エディタのマテリアルのデータ
	/// @param info 結果。info.texture_ids は data の中の texture_ids[] を指す(data より長く使わない)
	/// @return データが正しければ true
	/// NOTE: Primal はデータの中のポインタの場所に書き込み、その場所を material_init_info として渡すが、
	///       その場所は8バイトにそろっていないことがあるので、ここでは info にコピーして渡す。
	bool unpack_material_data(const u8* data, graphics::material_init_info& info);

	/// @brief エディタから渡されたアセットのデータからエンジンのリソースを作る
	/// @param data アセットの PackForEngine() の結果
	/// @param type アセットの種類(content::asset_type::type)。material、mesh、texture だけを扱う
	/// @return 作ったリソースのID。データが正しくない、または扱わない種類なら id::invalid_id
	id::id_type create_resource(const u8* data, u32 type);

	/// @brief create_resource() で作ったリソースを破棄する。無効なIDや扱わない種類は何もしない
	void destroy_resource(id::id_type id, u32 type);
}
