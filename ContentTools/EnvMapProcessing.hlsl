// EnvMapProcessing.hlsl
// Environment map processing compute shaders used by ContentTools (D3D11, cs_5_0).
// Compiled by CompileShaders.cmd into x64/Compiled/EnvMapProcessing_<entry point>.inc.
// Ported from Primal (Day 072, Day 073). Keep the comments in this file ASCII-only.
const static float PI = 3.1415926535897932f;
const static float SAMPLE_OFFSET = 0.5f;

// Must match ShaderConstants in EnvMapProcessing.cpp.
cbuffer Constants : register(b0)
{
    uint  g_CubeMapInSize;
    uint  g_CubeMapOutSize;
    uint  g_SampleCount;
    float g_Roughness;
};

Texture2D<float4>               EnvMap                  : register(t0);
TextureCube<float4>             CubeMapIn               : register(t0);

RWTexture2DArray<float4>        Output                  : register(u0);

SamplerState                    LinearSampler           : register(s0);

float3 GetSampleDirectionEquirectangular(uint face, float x, float y)
{
    float3 direction[6] = {
        {-x,    1.f, -y},   // X+ Left
        { x,   -1.f, -y},   // X- Right
        { y,    x,    1.f}, // Y+ Bottom
        {-y,    x,   -1.f}, // Y- Top
        { 1.f,  x,   -y},   // Z+ Front
        {-1.f, -x,   -y},   // Z- Back
    };

    return normalize(direction[face]);
}

// Direction of texel (x, y) of a cubemap face, following the D3D cubemap face layout, so
// sampling CubeMapIn in this direction at a texel center returns exactly that texel.
// Must match get_sample_direction_cubemap() in EnvMapProcessing.cpp.
float3 GetSampleDirectionCubemap(uint face, float x, float y)
{
    float3 direction[6] = {
        { 1.f, -y,   -x},   // X+ Left
        {-1.f, -y,    x},   // X- Right
        { x,    1.f,  y},   // Y+ Bottom
        { x,   -1.f, -y},   // Y- Top
        { x,   -y,    1.f}, // Z+ Front
        {-x,   -y,   -1.f}, // Z- Back
    };

    return normalize(direction[face]);
}

float2 DirectionToEquirectangularUV(float3 dir)
{
    float Phi = atan2(dir.y, dir.x);
    float Theta = acos(dir.z);
    float u = Phi * (0.5f / PI) + 0.5f;
    float v = Theta * (1.f / PI);
    return float2(u, v);
}

// Irradiance (divided by PI) around the normal: the cosine-weighted average of the radiance over
// the hemisphere, summed over every texel center of mip 0 of all 6 input faces.
// 4 / (1 + x^2 + y^2)^(3/2) is proportional to the solid angle of the texel at (x, y).
// NOTE: Primal Day 073 multiplied the radiance by cosTheta twice (cos^2 weighting), which turns
//       a uniform input c into 2c/3. Primal fixed it in Day 074 (c216192); this is the fixed form.
// The CPU version (prefilter_diffuse() in EnvMapProcessing.cpp) computes the same sum.
float3 SampleHemisphereBrute(float3 normal)
{
    float3 irradiance = 0.f;
    float sampleCount = 0.f;
    float invDim = 1.f / g_CubeMapInSize;

    for (uint face = 0; face < 6; ++face)
        for (uint y = 0; y < g_CubeMapInSize; ++y)
            for (uint x = 0; x < g_CubeMapInSize; ++x)
            {
                float2 uv = (float2(x, y) + SAMPLE_OFFSET) * invDim;
                float2 pos = 2.f * uv - 1.f;

                float3 sampleDir = GetSampleDirectionCubemap(face, pos.x, pos.y);
                float cosTheta = dot(sampleDir, normal);
                if (cosTheta > 0.f)
                {
                    float tmp = 1.f + pos.x * pos.x + pos.y * pos.y;
                    float weight = 4.f * cosTheta / (sqrt(tmp) * tmp);
                    irradiance += CubeMapIn.SampleLevel(LinearSampler, sampleDir, 0).rgb * weight;
                    sampleCount += weight;
                }
            }

    irradiance *= 1.f / sampleCount;
    return irradiance;
}

[numthreads(16, 16, 1)]
void EquirectangularToCubeMapCS(uint3 DispatchThreadID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    uint face = GroupID.z;
    uint size = g_CubeMapOutSize;

    if (DispatchThreadID.x >= size || DispatchThreadID.y >= size || face >= 6) return;

    float2 uv = (float2(DispatchThreadID.xy) + SAMPLE_OFFSET) / size;
    float2 pos = 2.f * uv - 1.f;
    float3 sampleDirection = GetSampleDirectionEquirectangular(face, pos.x, pos.y);
    float2 dir = DirectionToEquirectangularUV(sampleDirection);
    // Misusing sampleCount as a toggle for mirroring the cubemap.
    if (g_SampleCount == 1) dir.x = 1.f - dir.x;
    float4 envMapSample = EnvMap.SampleLevel(LinearSampler, dir, 0);

    Output[uint3(DispatchThreadID.x, DispatchThreadID.y, face)] = envMapSample;
}


// Output: the diffuse IBL (irradiance) cubemap. Each output texel is sampled at its center (SAMPLE_OFFSET).
[numthreads(16, 16, 1)]
void PrefilterDiffuseEnvMapCS(uint3 DispatchThreadID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    uint face = GroupID.z;
    uint size = g_CubeMapOutSize;

    if (DispatchThreadID.x >= size || DispatchThreadID.y >= size || face >= 6) return;

    float2 uv = (float2(DispatchThreadID.xy) + SAMPLE_OFFSET) / size;
    float2 pos = 2.f * uv - 1.f;
    float3 sampleDirection = GetSampleDirectionCubemap(face, pos.x, pos.y);
    float3 irradiance = SampleHemisphereBrute(sampleDirection);

    Output[uint3(DispatchThreadID.x, DispatchThreadID.y, face)] = float4(irradiance, 1.f);
}
