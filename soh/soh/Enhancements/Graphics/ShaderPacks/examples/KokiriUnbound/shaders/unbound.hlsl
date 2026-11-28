#include "lib/lighting.hlsli"
#include "/lib/water.hlsli"
#if KOKIRI_MATERIAL_TRANSMISSION
#include "/lib/transmission.hlsli"
#endif
#if KOKIRI_SHADOWED_WATER
#define KOKIRI_SUN_MAP_SLOT t3
#include "/lib/sunshadow.hlsli"
#endif

#ifndef KOKIRI_DEMO_GLASS
#define KOKIRI_DEMO_GLASS 0
#endif
#ifndef KOKIRI_DEMO_LANTERNS
#define KOKIRI_DEMO_LANTERNS 1
#endif

Texture2D<float4> ContactField : register(t2);

float FilteredShadow(float2 uv, float distance) {
    uint2 size;
    ContactField.GetDimensions(size.x, size.y);
    float2 pixel = uv * size - 0.5;
    int2 base = int2(floor(pixel));
    float2 f = frac(pixel);
    float weighted = 0.0, weights = 0.0;
    float tolerance = max(4.0, distance * 0.016);
    [unroll] for (int y = 0; y < 2; ++y) {
        [unroll] for (int x = 0; x < 2; ++x) {
            int2 coord = clamp(base + int2(x, y), 0, int2(size) - 1);
            float2 sampleValue = ContactField.Load(int3(coord, 0)).rg;
            float2 spatial = 1.0 - abs(float2(x, y) - f);
            float bilateral = 1.0 - smoothstep(tolerance, 2.7 * tolerance, abs(distance - sampleValue.g));
            float weight = spatial.x * spatial.y * bilateral;
            weighted += weight * sampleValue.r;
            weights += weight;
        }
    }
    return weighted / max(0.15, weights);
}

float4 main(float4 position : SV_Position) : SV_Target {
    uint2 size;
    SceneColor.GetDimensions(size.x, size.y);
    int3 pixel = int3(int2(position.xy), 0);
    float4 scene = SceneColor.Load(pixel);
    uint area = uint(SceneData[0].x);
    bool unbound = UnboundArea(area);
#if KOKIRI_MATERIAL_TRANSMISSION
    if (!unbound && TransmissionData[0].y < 1.0) return scene;
#else
    if (!unbound) return scene;
#endif

    float2 uv = position.xy / size;
    float rawDepth = SceneDepth.Load(pixel);
    float distance = ViewDepth(rawDepth);
    float3 viewPoint = ViewPosition(uv, distance);
    float3x3 inverseView = ViewToWorld();
    float3 eye = mul(-View[3].xyz, inverseView);
    float3 surface = eye + mul(viewPoint, inverseView);
    float3 normal = SurfaceNormal(surface, eye, distance);
    float3 sun = SceneSun();
    float3 color = ToLightSpace(scene.rgb);

#if KOKIRI_MATERIAL_TRANSMISSION
    if (rawDepth < 0.999999) {
        color += TransmittedLight(surface, normal) * (0.32 + 0.55 * color);
    }
    if (!unbound) return float4(saturate(ToDisplay(color)), scene.a);
#endif

    if (rawDepth < 0.999999) {
        float shadow = FilteredShadow(uv, distance);
        float sunlight = saturate(dot(normal, sun));
        float warmth = dot(SceneData[3].rgb, float3(0.28, 0.55, 0.17));
        color *= lerp(1.0, float3(0.52, 0.65, 0.75), 0.82 * shadow);
        color *= 1.0 + sunlight * warmth * (1.0 - shadow) * float3(0.14, 0.093, 0.018);
        color += LocalLight(surface, normal, KOKIRI_DEMO_GLASS && area == 0x55u) * (0.20 + 0.48 * color);
    }

    WaterHit water = FindWater(eye, surface, distance);
    if (water.coverage > 0.0) {
        float sunlight = 1.0;
        float3 localLight = 0.0;
#if KOKIRI_SHADOWED_WATER
        float3 upward = float3(0.0, 1.0, 0.0);
        sunlight = SunDaylight() * (1.0 - SunOcclusion(water.surface, upward));
        localLight = LocalLight(water.surface, upward, KOKIRI_DEMO_GLASS && area == 0x55u);
#endif
        color = lerp(color, ShadeWater(water, eye, surface, uv, size, sun, sunlight, localLight), water.coverage);
    }
    WaterPath waterPath = FindWaterPath(eye, surface);
    if (waterPath.distance > 0.0) {
        color = ShadeUnderwater(color, waterPath, surface, sun, rawDepth < 0.999999);
    }

#if KOKIRI_DEMO_GLASS
    if (area == 0x55u) {
        Pane first = KokiriPane(0), second = KokiriPane(1);
#if KOKIRI_DEMO_LANTERNS
        if (rawDepth < 0.999999) {
            float3 tint = ProjectedGlass(surface, normal, first) + ProjectedGlass(surface, normal, second);
#if KOKIRI_MIXED_GLASS
            color += tint * (0.66 + 0.27 * color);
#else
            color += tint * (0.38 + 0.48 * color);
#endif
        }
#endif
        color = DrawGlass(color, eye, surface, first);
        color = DrawGlass(color, eye, surface, second);
    }
#endif

    return float4(saturate(ToDisplay(color)), scene.a);
}
