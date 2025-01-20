#if !defined(DXFORGE_COMMON_HLSLI) && !defined(__cplusplus)
#error Do not include this header directly in shader files. Only include this file via Common.hlsli.
#endif

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
};

struct Plane
{
    float3 Normal;
    float Distance;
};
// ビュー・フラストゥム・プレーン（ビュー空間内）
// 面順：左、右、上、下
// フロントプレーンとバックプレーンは、ライトカリングコンピュートシェーダで計算される。
struct Frustum
{
    Plane Planes[4];
};

struct LightCullingDispatchParameters
{
    // 派遣されたグループの数。 (このパラメータはHLSLのシステム値としては使用できません！)
    uint2 NumThreadGroups;
    // ディスパッチされたスレッドの総数。 (HLSLのシステム値としても利用できません!)
    // NOTE: この値は、実際に実行されたスレッド数よりも少ないかもしれない。
    //       スクリーンサイズがブロックサイズで均等に割り切れない場合。
    uint2 NumThreads;
    // カリングするライトの数（カリングできないので、並行光源は含まない）。
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
    float ConeRadius;

    uint Type;
    float3 _pad;
};

// フォーマットされ、D3D定数/構造化バッファに連続したチャンクとしてコピーする準備ができているライトデータが含まれています。
struct LightParameters
{
    float3 Position;
    float Intensity;

    float3 Direction;
    uint Type;

    float3 Color;
    float Range;

    float3 Attenuation;
    float CosUmbra;

    float CosPenumbra;
    float3 _pad;
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