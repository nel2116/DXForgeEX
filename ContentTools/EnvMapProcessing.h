// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [EnvMapProcessing.h]
// 作成日 : 2026/10/18
// 作成者 : 田中ミノル
// 概要 :
// 環境マップ(正距円筒のHDR画像 -> キューブマップ)の変換処理
// 更新履歴
// 2026/10/18 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "ToolsCommon.h"
#include <DirectXTex.h>

namespace dxforge::tools
{
	/// @brief 正距円筒の環境マップをキューブマップに変換する(GPU版, D3D11のコンピュートシェーダを使用)
	/// @param device D3D11デバイス
	/// @param env_maps 正距円筒の環境マップ(すべて同じサイズとフォーマット)
	/// @param env_map_count 環境マップの数(= 出力するキューブマップの数)
	/// @param cubemap_size キューブマップの1面のサイズ
	/// @param use_prefilter_size trueの場合はcubemap_sizeを無視してプリフィルタ用のサイズを使う
	/// @param mirror_cubemap キューブマップを左右反転するかどうか
	/// @param cubemaps 出力先
	/// @return 結果
	HRESULT equirectangular_to_cubemap(ID3D11Device* device, const DirectX::Image* env_maps, u32 env_map_count, u32 cubemap_size,
									   bool use_prefilter_size, bool mirror_cubemap, DirectX::ScratchImage& cubemaps);

	/// @brief 正距円筒の環境マップをキューブマップに変換する(CPU版, GPUが使えない場合のフォールバック)
	/// @param env_maps 正距円筒の環境マップ(すべて同じサイズとフォーマット)
	/// @param env_map_count 環境マップの数(= 出力するキューブマップの数)
	/// @param cubemap_size キューブマップの1面のサイズ
	/// @param use_prefilter_size trueの場合はcubemap_sizeを無視してプリフィルタ用のサイズを使う
	/// @param mirror_cubemap キューブマップを左右反転するかどうか
	/// @param cubemaps 出力先
	/// @return 結果
	HRESULT equirectangular_to_cubemap(const DirectX::Image* env_maps, u32 env_map_count, u32 cubemap_size,
									   bool use_prefilter_size, bool mirror_cubemap, DirectX::ScratchImage& cubemaps);
}
