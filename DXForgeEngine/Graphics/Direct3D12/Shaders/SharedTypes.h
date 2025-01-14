// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
// [SharedTypes.h]
// 作成日 : 2025/01/14
// 作成者 : 田中ミノル
// 概要
// 　共有型
// 更新履歴
// 2025/01/14 新規作成
// _/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/_/
#pragma once
// ====== インクルード部 ======
#include "Graphics/Direct3D12/D3D12CommonHeaders.h"

namespace dxforge::graphics::d3d12::hlsl
{
	using float4x4 = math::m4x4a;
	using float4 = math::v4;
	using float3 = math::v3;
	using float2 = math::v2;
	using uint4 = math::u32v4;
	using uint3 = math::u32v3;
	using uint2 = math::u32v2;
	using uint = u32;

#include "CommonTypes.hlsli"
}