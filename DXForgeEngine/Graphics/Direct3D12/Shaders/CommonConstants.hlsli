#if !defined(DXFORGE_COMMON_HLSLI) && !defined(__cplusplus)
#error Do not include this header directly in shader files. Only include this file via Common.hlsli.
#endif

// ライトタイプ
// NOTE: これらはdxforge::graphics::light::type enumerationと同じです！
static const uint LIGHT_TYPE_DIRECTIONAL_LIGHT = 0;
static const uint LIGHT_TYPE_POINT_LIGHT = 1;
static const uint LIGHT_TYPE_SPOTLIGHT = 2;