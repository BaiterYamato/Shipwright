#include "world.hlsli"

#ifndef KOKIRI_SUN_MAP_SLOT
#define KOKIRI_SUN_MAP_SLOT t2
#endif
Texture2D<float> SunShadowDepth : register(KOKIRI_SUN_MAP_SLOT);

float SunDaylight() {
    float day = SceneData[0].w;
    return smoothstep(0.25, 0.34, day) * (1.0 - smoothstep(0.66, 0.75, day));
}

float SunOcclusion(float3 surface, float3 normal) {
    if (SunShadowData[3].x < 0.5) return 0.0;
    float4 world = float4(surface, 1.0);
    float2 uv = float2(dot(world, SunShadowData[0]), dot(world, SunShadowData[1]));
    if (!InsideScreen(uv)) return 0.0;
    uint2 size;
    SunShadowDepth.GetDimensions(size.x, size.y);
    float texelWidth = SunShadowData[3].y;
    float facing = saturate(dot(normal, SunShadowData[2].xyz));
    float receiver = dot(world, SunShadowData[2]) + max(2.2, texelWidth * lerp(1.35, 0.57, facing));
    float depthScale = SunShadowData[3].w;
    float normalizedReceiver = (receiver - SunShadowData[3].z) * depthScale;
    if (depthScale > 0.0 && (normalizedReceiver <= 0.0 || normalizedReceiver >= 1.0)) return 0.0;
    float2 pixel = uv * size - 0.5;
    int2 center = int2(floor(pixel));
    float2 offset = frac(pixel);
    float blocked = 0.0;
    [unroll] for (int y = -1; y <= 2; ++y) {
        [unroll] for (int x = -1; x <= 2; ++x) {
            int2 coord = clamp(center + int2(x, y), 0, int2(size) - 1);
            float nearest = SunShadowDepth.Load(int3(coord, 0));
            float2 weight = 2.0 - abs(float2(x, y) - offset);
            float gap = depthScale > 0.0 ? (nearest - normalizedReceiver) / depthScale : nearest - receiver;
            blocked += weight.x * weight.y * smoothstep(0.0, max(texelWidth, 4.0), gap);
        }
    }
    float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
    return blocked * (smoothstep(0.0, 0.035, edge) / 16.0);
}
