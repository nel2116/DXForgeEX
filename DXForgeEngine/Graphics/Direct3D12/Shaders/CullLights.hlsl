#include "Common.hlsli"
// NOTE: この定数は、ライトカリングモジュールのmax_lights_per_tile（256と定義されている）より大きい。
//       というのも、256は1タイルあたりのライトの*平均*数の最大値だからだ。
//       この定数はタイルあたりの最大ライト数である。
static const uint MaxLightsPerGroup = 1024;
groupshared uint _minDepthVS; // タイルのビュー空間における最小深度。
groupshared uint _maxDepthVS; // ビュー空間におけるタイルの最大深度。
groupshared uint _lightCount; // このタイルのピクセルに影響を与えるライトの数。
groupshared uint _lightIndexStartOffset; // lightIndexListをコピーするグローバル・ライト・インデックス・リスト内のオフセット。
groupshared uint _lightIndexList[MaxLightsPerGroup]; // このタイルに影響を与えるライトのインデックス。

ConstantBuffer<GlobalShaderData> GlobalData : register(b0, space0);
ConstantBuffer<LightCullingDispatchParameters> ShaderParams : register(b1, space0);
StructuredBuffer<Frustum> Frustums : register(t0, space0);
StructuredBuffer<LightCullingLightInfo> Lights : register(t1, space0);

RWStructuredBuffer<uint> LightIndexCounter : register(u0, space0);
RWStructuredBuffer<uint2> LightGrid_Opaque : register(u1, space0);
RWStructuredBuffer<uint> LightIndexList_Opaque : register(u3, space0);


// ライトカリングシェーダーの実装は、以下のものに基づいている。
// "Forward vs Deferred vs Forward+ Rendering with DirectX 11" (2015) by Jeremiah van Oosten.
// https://www.3dgep.com/forward-plus/#light-culling
//
// NOTE: TILE_SIZEはコンパイル時にエンジンによって定義される。
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void CullLightsCS(ComputeShaderInput csIn)
{
    // 初期化セクション
    if (csIn.GroupIndex == 0) // グループの最初のスレッドだけがグループ共有メモリを初期化する必要がある。
    {
        _minDepthVS = 0x7f7fffff; // uintとしてのFLT_MAX
        _maxDepthVS = 0;
        _lightCount = 0;
    }
    uint i = 0, index = 0; // 再利用可能なインデックス変数。

    GroupMemoryBarrierWithGroupSync(); // グループ共有メモリの初期化が完了するまで待機する。
    // 深さ最小/最大セクション
    const float depth = 
    Texture2D( ResourceDescriptorHeap[ShaderParams.DepthBufferSrvIndex])[csIn.DispatchThreadID.xy].
    r;
    const float depthVS = ClipToView(float4(0.f, 0.f, depth, 1.f), GlobalData.InvProjection).z;
    // 右回りのコリナート（負のZ軸）のため、深さは負になる
    // こうすることで比較がわかりやすくなる。
    const uint z = asuint(-depthVS);
    if (depth != 0) // 遠い平面を含めない
    {
        InterlockedMin(_minDepthVS, z);
        InterlockedMax(_maxDepthVS, z);
    }

    GroupMemoryBarrierWithGroupSync(); // 深度の最小/最大が更新されるまで待機する。
    // ライトカリングセクション
    const uint gridIndex = csIn.GroupID.x + (csIn.GroupID.y * ShaderParams.NumThreadGroups.x);
    const Frustum frustum = Frustums[gridIndex];
    // ビュー・スペースの最小／最大をもう一度否定する。
    const float minDepthVS = -asfloat(_minDepthVS);
    const float maxDepthVS = -asfloat(_maxDepthVS);
    for (i = csIn.GroupIndex; i < ShaderParams.NumLights; i += TILE_SIZE * TILE_SIZE)
    {
        const LightCullingLightInfo light = Lights[i];
        const float3 lightPositionVS = mul(GlobalData.View, float4(light.Position, 1.f)).xyz;
        if (light.Type == LIGHT_TYPE_POINT_LIGHT)
        {
            const Sphere sphere = { lightPositionVS, light.Range };
            if (SphereInsideFrustum(sphere, frustum, minDepthVS, maxDepthVS))
            {
                InterlockedAdd(_lightCount, 1, index);
                if (index < MaxLightsPerGroup)
                    _lightIndexList[index] = i;
            }
        }
        else if (light.Type == LIGHT_TYPE_SPOTLIGHT)
        {
            const float3 lightDirectionVS = mul(GlobalData.View, float4(light.Direction, 0.f)).xyz;
            const Cone cone = { lightPositionVS, light.Range, lightDirectionVS, light.ConeRadius };
            if (ConeInsideFrustum(cone, frustum, minDepthVS, maxDepthVS))
            {
                InterlockedAdd(_lightCount, 1, index);
                if (index < MaxLightsPerGroup)
                    _lightIndexList[index] = i;
            }
        }
    }

    GroupMemoryBarrierWithGroupSync(); // ライトカリングが完了するまで待機する。
    // ライトグリッドセクションの更新
    const uint lightCount = min(_lightCount, MaxLightsPerGroup);
    if (csIn.GroupIndex == 0)
    {
        InterlockedAdd(LightIndexCounter[0], lightCount, _lightIndexStartOffset);
        LightGrid_Opaque[gridIndex] = uint2(_lightIndexStartOffset, lightCount);
    }

    GroupMemoryBarrierWithGroupSync(); // ライトグリッドセクションの更新が完了するまで待機する。
    // ライト・インデックス・リスト・セクションを更新
    for (i = csIn.GroupIndex; i < lightCount; i += TILE_SIZE * TILE_SIZE)
    {
        LightIndexList_Opaque[_lightIndexStartOffset + i] = _lightIndexList[i];
    }
}