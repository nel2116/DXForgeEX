#if !defined(DXFORGE_COMMON_HLSLI) && !defined(__cplusplus)
#error Do not include this header directly in shader files. Only include this file via Common.hlsli.
#endif

// 三角形を形成する3つの非直線点から平面を計算する。
// この式は、平面の法線の方向を決定するために、右回り（反時計回りの巻き順）の座標系を仮定している。
// 左回りの座標系を使用する場合は、cross(v0, v2)をcross(v2, v0)に置き換える。
Plane ComputePlane(float3 p0, float3 p1, float3 p2)
{
    Plane plane;
    const float3 v0 = p1 - p0;
    const float3 v2 = p2 - p0;
    plane.Normal = normalize(cross(v0, v2));
    plane.Distance = dot(plane.Normal, p0);
    return plane;
}
float4 ClipToView(float4 clip, float4x4 inverseProjection)
{
    float4 view = mul(inverseProjection, clip);
    view /= view.w;
    return view;
}
float4 ScreenToView(float4 screen, float2 invViewDimensions, float4x4 inverseProjection)
{
    // 正規化されたテクスチャ座標に変換する
    float2 texCoord = screen.xy * invViewDimensions;
    // クリップスペースに変換
    float4 clip = float4(float2(texCoord.x, 1.f - texCoord.y) * 2.f - 1.f, screen.z, screen.w);
    return ClipToView(clip, inverseProjection);
}