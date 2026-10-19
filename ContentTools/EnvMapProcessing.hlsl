// EnvMapProcessing.hlsl
// Environment map processing compute shaders used by ContentTools (D3D11, cs_5_0).
// Compiled by CompileShaders.cmd into x64/Compiled/EnvMapProcessing_<entry point>.inc.
// Ported from Primal (Day 072 - Day 075). Keep the comments in this file ASCII-only.
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

// Van der Corput radical inverse in base 2 (bit reversal).
// Must match radical_inverse_vdc() in EnvMapProcessing.cpp.
float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}

float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float Pow4(float x)
{
    float xx = x * x;
    return xx * xx;
}

float Pow5(float x)
{
    float xx = x * x;
    return xx * xx * x;
}

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

// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
// Same as V_SmithGGXCorrelated() in the engine's BRDF.hlsli. a is alpha^2 = roughness^4.
float V_SmithGGXCorrelated(float NoV, float NoL, float a)
{
    float GGXL = NoV * sqrt((-NoL * a + NoL) * NoL + a);
    float GGXV = NoL * sqrt((-NoV * a + NoV) * NoV + a);
    return 0.5f / (GGXV + GGXL);
}

// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
// a is alpha^2 = roughness^4.
float D_GGX(float NoH, float a)
{
    float d = (NoH * a - NoH) * NoH + 1.f;
    return a / (PI * d * d);
}

// Orthonormal basis whose z axis is the normal. mul(v, GetTangentFrame(n)) takes v from tangent space to world space.
float3x3 GetTangentFrame(float3 normal)
{
    float3 up = abs(normal.z) < 0.999f ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangentX = normalize(cross(up, normal));
    float3 tangentY = cross(normal, tangentX);

    return float3x3(tangentX, tangentY, normal);
}

// Half vector in tangent space, distributed by D_GGX(NoH, a) * NoH. a is alpha^2 = roughness^4.
float3 ImportanceSampleGGX(float2 E, float a)
{
    float Phi = 2.f * PI * E.x;
    float CosTheta = sqrt((1.f - E.y) / (1.f + (a - 1.f) * E.y));
    // NOTE: For a tiny a (a - 1 == -1 in float), CosTheta can round to slightly above 1, which made
    //       SinTheta NaN and silently dropped the sample (e.g. 32 of 1024 samples at roughness 0.5 / 256).
    //       Clamp like the CPU version (importance_sample_ggx() in EnvMapProcessing.cpp).
    float SinTheta = sqrt(max(1.f - CosTheta * CosTheta, 0.f));

    float3 H;
    H.x = SinTheta * cos(Phi);
    H.y = SinTheta * sin(Phi);
    H.z = CosTheta;

    return H;
}

// Scale (x) and bias (y) to F0 of the specular IBL (the second sum of the split sum approximation):
// the integral of the specular BRDF times NoL over the hemisphere, with the Schlick Fresnel term
// F0 + (1 - F0) * Fc split into F0 * (1 - Fc) and Fc. Uses the same GGX importance sampling as
// PrefilterEnvMap(), with N = (0, 0, 1) and V in the xz plane.
// [Karis 2013, "Real Shading in Unreal Engine 4"]
// The pdf of L is D * NoH / (4 * VoH), so D * Vis * NoL / pdf = 4 * Vis * NoL * VoH / NoH.
// For roughness 0 every sample is H = N, which gives (1 - Fc, Fc) with Fc = (1 - NoV)^5.
// The CPU version (integrate_brdf() in EnvMapProcessing.cpp) computes the same sum (in double precision).
float2 IntegrateBRDF(float NoV, float roughness)
{
    float a4 = Pow4(roughness);
    float3 V;
    V.x = sqrt(1.f - NoV * NoV); // sin
    V.y = 0.f;
    V.z = NoV; // cos
    float A = 0.f;
    float B = 0.f;
    uint NumSamples = g_SampleCount;

    for (uint i = 0; i < NumSamples; i++)
    {
        float2 Xi = Hammersley(i, NumSamples);
        float3 H = ImportanceSampleGGX(Xi, a4);
        float3 L = 2.f * dot(V, H) * H - V;

        float NoL = saturate(L.z);
        float NoH = saturate(H.z);
        float VoH = saturate(dot(V, H));

        if (NoL > 0.f)
        {
            float G = V_SmithGGXCorrelated(NoV, NoL, a4);
            float G_Vis = 4.f * NoL * G * VoH / NoH;
            float Fc = Pow5(1.f - VoH);
            A += (1.f - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return float2(A, B) / NumSamples;
}

// Pre-filtered radiance for the specular IBL (the first sum of the split sum approximation),
// assuming N == V == R: the NoL-weighted average of the radiance over GGX importance samples.
// [Karis 2013, "Real Shading in Unreal Engine 4"]
// Uses pre-filtered importance sampling: a sample with a low probability fetches from a lower
// mip of the input, so that the solid angle of the sample matches the solid angle of a texel.
// [Lagarde et al. 2014, "Moving Frostbite to Physically based rendering"] (GPU Gems 3, chapter 20)
// Since N == V, NoH == HoV and the pdf of L simplifies from D * NoH / (4 * HoV) to D / 4.
// NOTE: This is the form Primal settled on in Day 075 (00dc198), except for the texel solid
//       angle: Primal uses invOmegaP = 6 * size^2 * PI / 4, but the solid angle of a texel is
//       4 * PI / (6 * size^2), so 1 / omegaP = 6 * size^2 / (4 * PI).
// The CPU version (prefilter_specular_texel() in EnvMapProcessing.cpp) computes the same sum.
float3 PrefilterEnvMap(float roughness, float3 N)
{
    float a4 = Pow4(roughness);
    float3 V = N;

    float3 PrefilteredColor = 0;
    float TotalWeight = 0;
    uint NumSamples = g_SampleCount;
    float resolution = g_CubeMapInSize; // resolution of source cubemap (per face)
    float invOmegaP = (6.f * resolution * resolution) / (4.f * PI);
    float mipLevel = 0;
    float3x3 tangentFrame = GetTangentFrame(N);

    for (uint i = 0; i < NumSamples; i++)
    {
        float2 Xi = Hammersley(i, NumSamples);
        float3 H = mul(ImportanceSampleGGX(Xi, a4), tangentFrame);
        float3 L = 2.f * dot(V, H) * H - V;
        float NoL = saturate(dot(N, L));

        if (NoL > 0)
        {
            float NoH = saturate(dot(N, H));
            float pdf = D_GGX(NoH, a4) * 0.25f;
            float omegaS = 1.f / (float(NumSamples) * pdf + 0.0001f);

            mipLevel = roughness == 0.f ? 0.f : 0.5f * log2(omegaS * invOmegaP);

            PrefilteredColor += CubeMapIn.SampleLevel(LinearSampler, L, mipLevel).rgb * NoL;
            TotalWeight += NoL;
        }
    }
    return PrefilteredColor / TotalWeight;
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

// Output: the BRDF integration LUT (g_CubeMapOutSize x g_CubeMapOutSize). x is NoV and y is the (perceptual)
// roughness, both sampled at the texel center (SAMPLE_OFFSET), so that sampling the LUT at uv = (NoV, roughness)
// with a linear clamp sampler returns the integral at that point.
// NOTE: Primal Day 075 (00dc198) uses uv = DispatchThreadID / (size - 1), i.e. the texel corners.
[numthreads(16, 16, 1)]
void ComputeBrdfIntegrationLutCS(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    uint size = g_CubeMapOutSize;
    if (DispatchThreadID.x >= size || DispatchThreadID.y >= size) return;

    float2 uv = (float2(DispatchThreadID.xy) + SAMPLE_OFFSET) / size;
    float2 result = IntegrateBRDF(uv.x, uv.y);
    Output[uint3(DispatchThreadID.x, DispatchThreadID.y, 0)] = float4(result, 0.f, 1.f);
}

// Output: one roughness mip of the specular IBL cubemap (g_Roughness). Each output texel is sampled at its center (SAMPLE_OFFSET).
[numthreads(16, 16, 1)]
void PrefilterSpecularEnvMapCS(uint3 DispatchThreadID : SV_DispatchThreadID, uint3 GroupID : SV_GroupID)
{
    uint face = GroupID.z;
    uint size = g_CubeMapOutSize;

    if (DispatchThreadID.x >= size || DispatchThreadID.y >= size || face >= 6) return;

    float2 uv = (float2(DispatchThreadID.xy) + SAMPLE_OFFSET) / size;
    float2 pos = 2.f * uv - 1.f;
    float3 sampleDirection = GetSampleDirectionCubemap(face, pos.x, pos.y);
    float3 radiance = PrefilterEnvMap(g_Roughness, sampleDirection);

    Output[uint3(DispatchThreadID.x, DispatchThreadID.y, face)] = float4(radiance, 1.f);
}
