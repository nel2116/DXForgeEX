#include "Common.hlsli"

#if USE_BOUNDING_SPHERES
// NOTE: この定数は、ライトカリングモジュールのmax_lights_per_tile（256と定義されている）より大きい。
//       というのも、256は1タイルあたりのライトの*平均*数の最大値だからだ。
//       この定数はタイルあたりの最大ライト数である。
static const uint MaxLightsPerGroup = 1024;

groupshared uint _minDepthVS; // タイルのビュー空間における最小深度。
groupshared uint _maxDepthVS; // ビュー空間におけるタイルの最大深度。
groupshared uint _lightCount; // このタイルのピクセルに影響を与えるライトの数。
groupshared uint _lightIndexStartOffset; // lightIndexListをコピーするグローバル・ライト・インデックス・リスト内のオフセット。
groupshared uint _lightIndexList[MaxLightsPerGroup]; // このタイルに影響を与えるライトのインデックス。
groupshared uint _lightFlagsOpaque[MaxLightsPerGroup]; // は、実際にピクセルに影響を与えるタイル内のライトにフラグを立てる。
groupshared uint _spotlightStartOffset;
groupshared uint2 _opaqueLightIndex; // xはポイントライト、yはスポットライト。

ConstantBuffer<GlobalShaderData> GlobalData : register(b0, space0);
ConstantBuffer<LightCullingDispatchParameters> ShaderParams : register(b1, space0);
StructuredBuffer<Frustum> Frustums : register(t0, space0);
StructuredBuffer<LightCullingLightInfo> Lights : register(t1, space0);
StructuredBuffer<Sphere> BoundingSpheres : register(t2, space0);

RWStructuredBuffer<uint> LightIndexCounter : register(u0, space0);
RWStructuredBuffer<uint2> LightGrid_Opaque : register(u1, space0);
RWStructuredBuffer<uint> LightIndexList_Opaque : register(u3, space0);

Sphere GetConeBoundingSphere(float3 tip, float range, float3 direction, float cosPenumbra)
{
    Sphere sphere;
    sphere.Radius = range / (2.0f * cosPenumbra);
    sphere.Center = tip + sphere.Radius * direction;

    if (cosPenumbra < 0.707107f)
    {
        const float coneSin = sqrt(1.f - cosPenumbra * cosPenumbra);
        sphere.Center = tip + cosPenumbra * range * direction;
        sphere.Radius = coneSin * range;
    }

    return sphere;
}

bool Intersects(Frustum frustum, Sphere sphere, float minDepth, float maxDepth)
{
    if ((sphere.Center.z - sphere.Radius > minDepth) || (sphere.Center.z + sphere.Radius < maxDepth))
        return false;

    const float3 lightRejection = sphere.Center - dot(sphere.Center, frustum.ConeDirection) * frustum.ConeDirection;
    const float distSq = dot(lightRejection, lightRejection);
    const float radius = sphere.Center.z * frustum.UnitRadius + sphere.Radius;
    const float radiusSq = radius * radius;

    return distSq <= radiusSq;
}

// NOTE: TILE_SIZEはコンパイル時にエンジンによって定義される。
[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void CullLightsCS(ComputeShaderInput csIn)
{
    // INITIALIZATION SECTION
    //
    // 我々の右手座標系では、列長射影行列は次のようになる：
    //
    //      Projection:             Inverse projection:
    //      | A  0  0  0 |          | 1/A  0   0   0  |
    //      | 0  B  0  0 |          |  0  1/B  0   0  |
    //      | 0  0  C  D |          |  0   0   0  -1  |
    //      | 0  0 -1  0 |          |  0   0  1/D C/D |
    //
    // 位置ベクトルvをクリップからビュー空間に変換する：
    //
    // q = mul(inverse_projection, v);
    // v_viewSpace = q / q.w;
    //
    // However, we only need the z-component of v_viewSpace (for v = (0, 0, depth, 1)):
    //
    // v_viewSpace = -D / (depth + C);
    //
    const float depth =
    Texture2D( ResourceDescriptorHeap[ShaderParams.DepthBufferSrvIndex])[csIn.DispatchThreadID.xy].
    r;
    const float C = GlobalData.Projection._m22;
    const float D = GlobalData.Projection._m23;
    const uint gridIndex = csIn.GroupID.x + (csIn.GroupID.y * ShaderParams.NumThreadGroups.x);
    const Frustum frustum = Frustums[gridIndex];

    if (csIn.GroupIndex == 0) // only the first thread in the group need to initialize groupshared memory
    {
        _minDepthVS = 0x7f7fffff; // uintとしてのFLT_MAX
        _maxDepthVS = 0;
        _lightCount = 0;
        _opaqueLightIndex = 0;
    }

    uint i = 0, index = 0; // 再利用可能なインデックス変数。

    for (i = csIn.GroupIndex; i < MaxLightsPerGroup; i += TILE_SIZE * TILE_SIZE)
    {
        _lightFlagsOpaque[i] = 0;
    }

    // 深さ最小/最大断面
    GroupMemoryBarrierWithGroupSync();

    if (depth != 0) // 遠い平面を含めない
    {
        // 深さが逆なので、最小／最大を入れ替える
        const float depthMin = WaveActiveMax(depth);
        const float depthMax = WaveActiveMin(depth);

        if (WaveIsFirstLane())
        {
            // 右回りのコリナート（負のZ軸）のため、深さを負にする。
            // これで比較がわかりやすくなる。
            const uint zMin = asuint(D / (depthMin + C)); // -minDepthVS as uint
            const uint zMax = asuint(D / (depthMax + C)); // -maxDepthVS as uint
            InterlockedMin(_minDepthVS, zMin);
            InterlockedMax(_maxDepthVS, zMax);
        }
    }

    // ライトカリングセクション
    GroupMemoryBarrierWithGroupSync();

    const float minDepthVS = -asfloat(_minDepthVS);
    const float maxDepthVS = -asfloat(_maxDepthVS);

    for (i = csIn.GroupIndex; i < ShaderParams.NumLights; i += TILE_SIZE * TILE_SIZE)
    {
        Sphere sphere = BoundingSpheres[i];
        sphere.Center = mul(GlobalData.View, float4(sphere.Center, 1.f)).xyz;

        if (Intersects(frustum, sphere, minDepthVS, maxDepthVS))
        {
            InterlockedAdd(_lightCount, 1, index);
            if (index < MaxLightsPerGroup)
                _lightIndexList[index] = i;
        }
    }

    // 軽剪定セクション
    GroupMemoryBarrierWithGroupSync();

    const uint lightCount = min(_lightCount, MaxLightsPerGroup);
    const float2 invViewDimensions = 1.f / float2(GlobalData.ViewWidth, GlobalData.ViewHeight);
    // Get world position of this pixel.
    const float3 pos = UnprojectUV(csIn.DispatchThreadID.xy * invViewDimensions, depth, GlobalData.InvViewProjection).xyz;

    for (i = 0; i < lightCount; ++i)
    {
        index = _lightIndexList[i];
        const LightCullingLightInfo light = Lights[index];
        const float3 d = pos - light.Position;
        const float distSq = dot(d, d);

        if (distSq <= light.Range * light.Range)
        {
            // NOTE: -1は、ライトが点光源であることを意味している。 そうでなければスポットライト
            const bool isPointLight = light.CosPenumbra == -1.f;
            if (isPointLight || (dot(d * rsqrt(distSq), light.Direction) >= light.CosPenumbra))
            {
                _lightFlagsOpaque[i] = 2 - uint(isPointLight);
            }
        }
    }

    // ライトグリッドの更新セクション
    GroupMemoryBarrierWithGroupSync();
    if (csIn.GroupIndex == 0)
    {
        uint numPointLights = 0;
        uint numSpotlights = 0;

        for (i = 0; i < lightCount; ++i)
        {
            numPointLights += (_lightFlagsOpaque[i] & 1);
            numSpotlights += (_lightFlagsOpaque[i] >> 1);
        }

        InterlockedAdd(LightIndexCounter[0], numPointLights + numSpotlights, _lightIndexStartOffset);
        _spotlightStartOffset = _lightIndexStartOffset + numPointLights;
        LightGrid_Opaque[gridIndex] = uint2(_lightIndexStartOffset, (numPointLights << 16) | numSpotlights);
    }

    // ライトインデックスリストの更新セクション
    GroupMemoryBarrierWithGroupSync();

    uint pointIndex, spotIndex;

    for (i = csIn.GroupIndex; i < lightCount; i += TILE_SIZE * TILE_SIZE)
    {
        if (_lightFlagsOpaque[i] == 1)
        {
            InterlockedAdd(_opaqueLightIndex.x, 1, pointIndex);
            LightIndexList_Opaque[_lightIndexStartOffset + pointIndex] = _lightIndexList[i];

        }
        else if (_lightFlagsOpaque[i] == 2)
        {
            InterlockedAdd(_opaqueLightIndex.y, 1, spotIndex);
            LightIndexList_Opaque[_spotlightStartOffset + spotIndex] = _lightIndexList[i];
        }
    }
}
#else
// NOTE: この定数は、ライトカリングモジュールのmax_lights_per_tileより大きい（256と定義されている）。
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
    // INITIALIZATION SECTION
    if (csIn.GroupIndex == 0) // グループの最初のスレッドだけがグループ共有メモリを初期化する必要がある。
    {
        _minDepthVS = 0x7f7fffff; // uintとしてのFLT_MAX
        _maxDepthVS = 0;
        _lightCount = 0;
    }

    uint i = 0, index = 0; // 再利用可能なインデックス変数。

    // 深さ最小/最大断面
    GroupMemoryBarrierWithGroupSync();

    const float depth =

    Texture2D( ResourceDescriptorHeap[ShaderParams.DepthBufferSrvIndex])[csIn.DispatchThreadID.xy].
    r;
    const float depthVS = ClipToView(float4(0.f, 0.f, depth, 1.f), GlobalData.InvProjection).z;
    // 右回りのコリナート（負のZ軸）のため、深さは負になる
    // こうすることで比較がわかりやすくなる。
    const uint z = asuint(-depthVS);

    if (depth != 0) // Don't include far plane
    {
        InterlockedMin(_minDepthVS, z);
        InterlockedMax(_maxDepthVS, z);
    }

    // ライトカリングセクション
    GroupMemoryBarrierWithGroupSync();

    const uint gridIndex = csIn.GroupID.x + (csIn.GroupID.y * ShaderParams.NumThreadGroups.x);
    const Frustum frustum = Frustums[gridIndex];

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

    // ライトグリッドの更新セクション
    GroupMemoryBarrierWithGroupSync();

    const uint lightCount = min(_lightCount, MaxLightsPerGroup);

    if (csIn.GroupIndex == 0)
    {
        InterlockedAdd(LightIndexCounter[0], lightCount, _lightIndexStartOffset);
        LightGrid_Opaque[gridIndex] = uint2(_lightIndexStartOffset, lightCount);
    }

    // ライトインデックスリストの更新セクション
    GroupMemoryBarrierWithGroupSync();

    for (i = csIn.GroupIndex; i < lightCount; i += TILE_SIZE * TILE_SIZE)
    {
        LightIndexList_Opaque[_lightIndexStartOffset + i] = _lightIndexList[i];
    }
}
#endif