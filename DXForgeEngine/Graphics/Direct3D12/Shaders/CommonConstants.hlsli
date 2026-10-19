#if !defined(DXFORGE_COMMON_HLSLI) && !defined(__cplusplus)
#error Do not include this header directly in shader files. Only include this file via Common.hlsli.
#endif

static const float PI = 3.1415926535897932384626433832795f;

// ライトタイプ
// NOTE: これらはdxforge::graphics::light::type enumerationと同じです！
static const uint LIGHT_TYPE_DIRECTIONAL_LIGHT = 0;
static const uint LIGHT_TYPE_POINT_LIGHT = 1;
static const uint LIGHT_TYPE_SPOTLIGHT = 2;
static const uint LIGHT_TYPE_AMBIENT_LIGHT = 3;

// 無効なSRVのインデックス(u32_invalid_idと同じ)
static const uint INVALID_SRV_INDEX = 0xffffffff;