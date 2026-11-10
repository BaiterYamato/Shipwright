#include "world.hlsli"

float ContactShadow(float3 viewPoint, float3 normal, float3 toLight, float distance, uint2 size) {
    if (distance > 1650.0) return 0.0;
    float3 start = viewPoint + mul(float4(normal * 2.5, 0.0), View).xyz;
    float3 direction = mul(float4(toLight, 0.0), View).xyz;
    float shadow = 0.0;
    [loop] for (int i = 0; i < 10; ++i) {
        float t = (float(i) + 1.0) * 0.1;
        float3 samplePoint = start + direction * (4.0 + 118.0 * t * t);
        float2 uv;
        if (!ProjectView(samplePoint, uv)) break;
        float gap = -samplePoint.z - DepthAt(uv, size);
        float thickness = lerp(5.0, 22.0, t);
        float hit = smoothstep(1.5, 4.5, gap) * (1.0 - smoothstep(thickness, thickness * 2.4, gap));
        float edge = min(min(uv.x, 1.0 - uv.x), min(uv.y, 1.0 - uv.y));
        shadow = max(shadow, hit * (1.0 - 0.42 * t) * smoothstep(0.0, 0.035, edge));
    }
    return shadow * (1.0 - smoothstep(1100.0, 1650.0, distance));
}

float3 GlassTransmission(float3 light) {
    // The crimson pane has a warm artistic scatter component. Blue survives
    // the pane and combines with this red contribution in the requested look.
    float blueWeight = saturate((light.b - max(light.r, light.g)) * 1.6);
    float3 filter = float3(0.92, 0.065, lerp(0.058, 0.73, blueWeight));
    return light * filter + float3(0.62 * light.b * blueWeight, 0, 0);
}

struct Pane {
    float3 center;
    float3 source;
    float3 color;
};

Pane KokiriPane(int index) {
    Pane pane;
    // Two lanterns in the clearing outside Link's house, in world units.
    pane.center = float3(index == 0 ? -134.0 : 18.0, -34.0, 748.0);
#if KOKIRI_MIXED_GLASS
    pane.source = pane.center + float3(index == 0 ? -20.0 : 20.0, 60.0, -65.0);
    pane.color = index == 0 ? float3(1.0, 0.017, 0.010) : float3(0.010, 0.028, 1.0);
#else
    pane.source = pane.center + float3(0.0, 60.0, -65.0);
    pane.color = index == 0 ? float3(1.0, 0.88, 0.66) : float3(0.09, 0.12, 1.0);
#endif
    return pane;
}

bool HitPane(float3 origin, float3 ray, Pane pane, out float t, out float2 aperture) {
    t = (pane.center.z - origin.z) / (abs(ray.z) > 0.00001 ? ray.z : 0.00001);
    aperture = abs((origin + t * ray - pane.center).xy) / float2(51.0, 49.0);
    return abs(ray.z) > 0.00001 && t > 0.0 && max(aperture.x, aperture.y) < 1.0;
}

float PaneAperture(float2 aperture) {
    float border = max(aperture.x, aperture.y);
    float feather = 1.0 - smoothstep(0.81, 1.0, border);
    float mullion = lerp(0.30, 1.0, smoothstep(0.025, 0.095, min(aperture.x, aperture.y)));
    return feather * mullion;
}

float3 LocalLight(float3 surface, float3 normal, bool filterGlass) {
    float3 result = 0.0;
    [loop] for (int i = 0; i < min(8, int(SceneData[2].x)); ++i) {
        float4 source = SceneData[4 + i];
        float3 offset = source.xyz - surface;
        float distance = length(offset);
        if (distance >= source.w) continue;
        float energy = 1.0 - distance / source.w;
        float facing = saturate(dot(normal, offset / max(distance, 1.0)));
        float3 light = ToLightSpace(SceneData[12 + i].rgb);
        if (filterGlass) {
            [unroll] for (int paneIndex = 0; paneIndex < 2; ++paneIndex) {
                float t;
                float2 aperture;
                Pane pane = KokiriPane(paneIndex);
                if (HitPane(surface, offset, pane, t, aperture) && t < 1.0) {
#if KOKIRI_MIXED_GLASS
                    light *= pane.color * PaneAperture(aperture);
#else
                    light = GlassTransmission(light) * PaneAperture(aperture);
#endif
                }
            }
        }
        result += light * (energy * energy * (0.24 + 0.76 * facing));
    }
    return min(result, 1.0);
}

float3 ProjectedGlass(float3 surface, float3 normal, Pane pane) {
    float3 offset = pane.source - surface;
    float distance = length(offset);
    if (distance > 390.0) return 0.0;
    float t;
    float2 aperture;
    if (!HitPane(surface, offset, pane, t, aperture) || t >= 1.0) return 0.0;
    float incident = saturate(dot(normal, offset / max(distance, 1.0)));
    float attenuation = 1.0 - smoothstep(85.0, 390.0, distance);
#if KOKIRI_MIXED_GLASS
    float3 transmitted = pane.color;
#else
    float3 transmitted = GlassTransmission(pane.color);
#endif
    return transmitted * PaneAperture(aperture) * incident * attenuation;
}

float3 DrawGlass(float3 background, float3 eye, float3 surface, Pane pane) {
    float t;
    float2 aperture;
    if (!HitPane(eye, surface - eye, pane, t, aperture) || t >= 1.0) return background;
    float border = max(aperture.x, aperture.y);
    float bars = max(smoothstep(0.875, 0.925, border),
                     1.0 - smoothstep(0.021, 0.046, min(aperture.x, aperture.y)));
    float glass = (1.0 - bars) * (1.0 - smoothstep(0.964, 1.0, border));
#if KOKIRI_MIXED_GLASS
    float3 transmitted = background * (0.20 + 0.55 * pane.color) + 0.17 * pane.color;
#else
    float3 transmitted = background * float3(0.77, 0.29, 0.53) + 0.12 * GlassTransmission(pane.color);
#endif
    return lerp(lerp(background, transmitted, glass), float3(0.13, 0.067, 0.031),
                bars * (1.0 - smoothstep(0.984, 1.0, border)));
}
