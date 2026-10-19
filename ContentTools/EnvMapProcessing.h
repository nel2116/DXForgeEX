// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [EnvMapProcessing.h]
// 作成日 : 2026/10/18
// 作成者 : 田中ミノル
// 概要 :
// 環境マップ(正距円筒のHDR画像 -> キューブマップ)の変換処理
// 更新履歴
// 2026/10/18 新規作成
// 2026/10/18 拡散光のIBL用にキューブマップを事前フィルタする処理(irradiance map)を追加 (Primal Day 073)
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

	/// @brief キューブマップを拡散光のIBL用に事前フィルタする(irradiance map, GPU版, D3D11のコンピュートシェーダを使用)
	/// @details 出力の各画素の方向を法線とする半球について、入力のミップ0の全画素の放射輝度をcosθと立体角で重み付けた平均(放射照度 / π)を求める。
	///          入力と出力のどちらも画素の中心からサンプリングする。
	/// @param device D3D11デバイス
	/// @param cubemaps 入力のキューブマップ(1つ以上, ブロック圧縮されていないこと)
	/// @param prefiltered_diffuse 出力先(入力と同じ数とフォーマットの、1面が64x64でミップ1つのキューブマップ)
	/// @return 結果
	HRESULT prefilter_diffuse(ID3D11Device* device, const DirectX::ScratchImage& cubemaps, DirectX::ScratchImage& prefiltered_diffuse);

	/// @brief キューブマップを拡散光のIBL用に事前フィルタする(irradiance map, CPU版, GPUが使えない場合のフォールバック)
	/// @details GPU版と同じ和を求める(GPU版との違いは浮動小数点の丸め誤差だけ)。
	/// @param cubemaps 入力のキューブマップ(1つ以上, ブロック圧縮されていないこと)
	/// @param prefiltered_diffuse 出力先(入力と同じ数とフォーマットの、1面が64x64でミップ1つのキューブマップ)
	/// @return 結果
	HRESULT prefilter_diffuse(const DirectX::ScratchImage& cubemaps, DirectX::ScratchImage& prefiltered_diffuse);
}
