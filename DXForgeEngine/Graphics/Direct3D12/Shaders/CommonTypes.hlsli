#if !defined(DXFORGE_COMMON_HLSLI) && !defined(__cplusplus)
#error Do not include this header directly in shader files. Only include this file via Common.hlsli.
#endif

#define USE_BOUNDING_SPHERES 1

struct GlobalShaderData
{
    float4x4 View;
    float4x4 Projection;
    float4x4 InvProjection;
    float4x4 ViewProjection;
    float4x4 InvViewProjection;

    float3 CameraPosition;
    float ViewWidth;

    float3 CameraDirection;
    float ViewHeight;

    uint NumDirectionalLights;
    float DeltaTime;
};

struct PerObjectData
{
    float4x4 World;
    float4x4 InvWorld;
    float4x4 WorldViewProjection;

    float4 BaseColor;
    float3 Emissive;
    float EmissiveIntensity;
    float AmbientOcclusion;
    float Metallic;
    float Roughness;
    uint _pad;
};

struct Plane
{
    float3 Normal;
    float Distance;
};

struct Sphere
{
    float3 Center;
    float Radius;
};

struct Cone
{
    float3 Tip;
    float Height;
    float3 Direction;
    float Radius;
};

#if USE_BOUNDING_SPHERES
// 視界空間におけるフルストゥムコーン
struct Frustum
{
    float3 ConeDirection;
    float UnitRadius;
};

#else
// ビューフラストゥムプレーン（ビュー空間内）
// 面順：左、右、上、下
// フロントプレーンとバックプレーンはライトカリングで計算される。r.
struct Frustum
{
    Plane Planes[4];
};
#endif

#ifndef __cplusplus
struct ComputeShaderInput
{
    uint3 GroupID : SV_GroupID; // ディスパッチ内のスレッドグループの3Dインデックス。
    uint3 GroupThreadID : SV_GroupThreadID; // スレッドグループ内のローカルスレッドIDの3Dインデックス。
    uint3 DispatchThreadID : SV_DispatchThreadID; // ディスパッチ内のグローバルスレッドIDの3Dインデックス。
    uint GroupIndex : SV_GroupIndex; // スレッドグループ内のスレッドの平坦化されたローカルインデックス。
};
#endif

struct LightCullingDispatchParameters
{
    // 派遣されたグループの数。 (このパラメータはHLSLのシステム値としては使用できません！)
    uint2 NumThreadGroups;

    // ディスパッチされたスレッドの総数。 (HLSLのシステム値としても利用できません!)
    // NOTE: スクリーンサイズがブロックサイズで均等に割り切れない場合、
    //       この値は実際に実行されるスレッド数より少なくなる可能性がある。
    uint2 NumThreads;

    // カリングするライトの数（カリングできないので、指向性ライトは含まない）。
    uint NumLights;

    // SRVディスクリプタヒープ内のカレントデプスバッファのインデックス
    uint DepthBufferSrvIndex;
};

// フォーマットされ、連続したチャンクとしてD3Dの定数/構造化バッファにコピーする準備ができたライトカラインデータが含まれています。
struct LightCullingLightInfo
{
    float3 Position;
    float Range;

    float3 Direction;
#if USE_BOUNDING_SPHERES
    // これが-1に設定されている場合、ライトは点光源となる。
    float CosPenumbra;
#else
    float   ConeRadius;

    uint    Type;
    float3  _pad;
#endif
};

// フォーマットされ、D3D定数/構造化バッファに連続したチャンクとしてコピーする準備ができているライトデータが含まれています。
struct LightParameters
{
    float3 Position;
    float Intensity;

    float3 Direction;
    float Range;

    float3 Color;
    float CosUmbra; // アンブラのコサイン

    float3 Attenuation;
    float CosPenumbra; // ペナンブラのコサイン

#if !USE_BOUNDING_SPHERES
    uint    Type;
    float3  _pad;
#endif

};

struct DirectionalLightParameters
{
    float3 Direction;
    float Intensity;

    float3 Color;
    float _pad;
};

#ifdef __cplusplus
static_assert((sizeof(PerObjectData) % 16) == 0,
              "Make sure PerObjectData is formatted in 16-byte chunks without any implicit padding.");
static_assert((sizeof(LightParameters) % 16) == 0,
              "Make sure LightParameters is formatted in 16-byte chunks without any implicit padding.");
static_assert((sizeof(LightCullingLightInfo) % 16) == 0,
              "Make sure LightCullingLightInfo is formatted in 16-byte chunks without any implicit padding.");
static_assert((sizeof(DirectionalLightParameters) % 16) == 0,
              "Make sure DirectionalLightParameters is formatted in 16-byte chunks without any implicit padding.");
#endif