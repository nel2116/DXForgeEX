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

// 点が平面の完全に後ろ（負の半値空間の内側）にあるかどうかをチェックする。
bool PointInsidePlane(float3 p, Plane plane)
{
    return dot(plane.Normal, p) - plane.Distance < 0;
}

// 球が平面の完全に後ろ（負の半値空間の内側）にあるかどうかをチェックする。
// Source: Real-time collision detection, Christer Ericson (2005)
bool SphereInsidePlane(Sphere sphere, Plane plane)
{
    return dot(plane.Normal, sphere.Center) - plane.Distance < -sphere.Radius;
}

// 円錐が完全に平面の後ろ（平面の負の半値空間の内側）にあるかどうかをチェックする。
// Source: Real-time collision detection, Christer Ericson (2005)
bool ConeInsidePlane(Cone cone, Plane plane)
{
    // 円錐の端から平面の正の空間までの最も遠い点を計算する。
    float3 m = cross(cross(plane.Normal, cone.Direction), cone.Direction);
    float3 Q = cone.Tip + cone.Direction * cone.Height - m * cone.Radius;

    // 円錐の先端と、平面の正半分空間に対して円錐の端の最も遠い点の両方が平面の負半分空間内にある場合
    // 円錐は平面の負半分空間内にある。
    return PointInsidePlane(cone.Tip, plane) && PointInsidePlane(Q, plane);
}

// ライトの一部がフラストラムに収まっているか確認する。
bool SphereInsideFrustum(Sphere sphere, Frustum frustum, float zNear, float zFar)
{
    // 最初に深さを確認する
    // NOTE: ここで、ビューベクトルは-Z軸を指しているので、遠景の深度値は-∞に近づく。
    return !((sphere.Center.z - sphere.Radius > zNear || sphere.Center.z + sphere.Radius < zFar) ||
             SphereInsidePlane(sphere, frustum.Planes[0]) ||
             SphereInsidePlane(sphere, frustum.Planes[1]) ||
             SphereInsidePlane(sphere, frustum.Planes[2]) ||
             SphereInsidePlane(sphere, frustum.Planes[3]));
}

bool ConeInsideFrustum(Cone cone, Frustum frustum, float zNear, float zFar)
{
    bool result = true;

    Plane nearPlane = { float3(0, 0, -1), -zNear };
    Plane farPlane = { float3(0, 0, 1), zFar };

    // まず、近距離と遠距離のクリッピングプレーンをチェックする。
    if (ConeInsidePlane(cone, nearPlane) || ConeInsidePlane(cone, farPlane))
    {
        result = false;
    }

    // 次に、フラストラムプレーンをチェックする。
    for (int i = 0; i < 4 && result; i++)
    {
        if (ConeInsidePlane(cone, frustum.Planes[i]))
        {
            result = false;
        }
    }

    return result;
}

float4 ClipToView(float4 clip, float4x4 inverseProjection)
{
    // View space position
    float4 view = mul(inverseProjection, clip);
    // Perspective (un)projection
    view /= view.w;
    return view;
}

float4 ScreenToView(float4 screen, float2 invViewDimensions, float4x4 inverseProjection)
{
    // Convert to normalized texture coordinates
    float2 texCoord = screen.xy * invViewDimensions;

    // Convert to clip space
    float4 clip = float4(float2(texCoord.x, 1.f - texCoord.y) * 2.f - 1.f, screen.z, screen.w);

    return ClipToView(clip, inverseProjection);
}