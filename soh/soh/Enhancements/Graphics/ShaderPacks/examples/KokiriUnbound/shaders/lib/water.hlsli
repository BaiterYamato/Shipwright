#include "world.hlsli"

struct WaterHit {
    float3 surface;
    float depth;
    float coverage;
};

WaterHit FindWater(float3 eye, float3 worldPoint, float distance) {
    WaterHit water = (WaterHit)0;
    water.depth = distance + 2.0;
    float3 ray = worldPoint - eye;
    if (ray.y > -0.001) return water;
    [loop] for (int i = 0; i < min(8, int(SceneData[2].y)); ++i) {
        float height = SceneData[28 + i].x;
        if (eye.y <= height) continue;
        float t = (height - eye.y) / ray.y;
        float hitDepth = t * distance;
        if (hitDepth >= water.depth || hitDepth <= 0.0) continue;
        float4 bounds = SceneData[20 + i];
        float3 hit = eye + ray * t;
        float2 edge = min(hit.xz - bounds.xy, bounds.zw - hit.xz);
        float margin = min(edge.x, edge.y);
        if (margin <= 0.0) continue;
        float submerged = height - worldPoint.y;
        water.surface = hit;
        water.depth = hitDepth;
        water.coverage = smoothstep(-1.0, 1.4, submerged) * smoothstep(0.0, 8.0, margin) *
                         smoothstep(0.0, 8.0, eye.y - height);
    }
    return water;
}

bool ClipWaterAxis(float origin, float direction, float lower, float upper, inout float entry, inout float exit) {
    if (abs(direction) < 0.00001) return origin > lower && origin < upper;
    float2 bounds = (float2(lower, upper) - origin) / direction;
    entry = max(entry, min(bounds.x, bounds.y));
    exit = min(exit, max(bounds.x, bounds.y));
    return exit > entry;
}

struct WaterPath {
    float distance;
    float depth;
    bool submergedSurface;
};

WaterPath FindWaterPath(float3 eye, float3 surface) {
    WaterPath path = (WaterPath)0;
    float3 ray = surface - eye;
    [loop] for (int i = 0; i < min(8, int(SceneData[2].y)); ++i) {
        float height = SceneData[28 + i].x;
        float immersion = height - eye.y;
        if (immersion <= 0.0) continue;
        float4 bounds = SceneData[20 + i];
        float entry = 0.0;
        float exit = ray.y > 0.00001 ? min(1.0, immersion / ray.y) : 1.0;
        if (!ClipWaterAxis(eye.x, ray.x, bounds.x, bounds.z, entry, exit) ||
            !ClipWaterAxis(eye.z, ray.z, bounds.y, bounds.w, entry, exit)) continue;
        float distance = (exit - entry) * length(ray) * smoothstep(0.0, 7.0, immersion);
        if (distance <= path.distance) continue;
        path.distance = distance;
        path.depth = max(0.0, height - surface.y);
        path.submergedSurface = exit >= 0.9999 && surface.y <= height;
    }
    return path;
}

float WaterRipple(float2 surface, float time) {
    return sin(dot(surface, float2(0.13, 0.17)) + time * 2.1) +
           sin(dot(surface, float2(0.18, -0.11)) - time * 1.7);
}

float3 ShadeUnderwater(float3 color, WaterPath path, float3 surface, float3 sun, bool solid) {
    float3 transmission = exp(-min(path.distance, 4000.0) * float3(0.0045, 0.0019, 0.00125));
    float brightness = smoothstep(0.0, 0.65, sun.y);
    float3 scatter = float3(0.013, 0.083, 0.106) * lerp(0.55, 1.0, brightness);
    if (solid && path.submergedSurface) {
        float wave = WaterRipple(surface.xz, SceneData[0].z);
        float caustic = pow(saturate(0.47 + 0.26 * wave), 5.0);
        color += float3(0.044, 0.102, 0.100) * caustic * exp(-0.007 * path.depth) *
                 smoothstep(0.0, 12.0, path.distance) * brightness;
    }
    return color * transmission + scatter * (1.0 - transmission);
}

float3 WaterNormal(float2 surface, float time) {
    float2 a = float2(0.071, 0.043);
    float2 b = float2(-0.037, 0.081);
    float2 c = float2(0.132, -0.097);
    float2 slope = a * (1.45 * cos(dot(surface, a) + 1.42 * time)) +
                   b * (0.91 * cos(dot(surface, b) - 1.67 * time)) +
                   c * (0.46 * cos(dot(surface, c) + 2.25 * time));
    return normalize(float3(-slope.x, 1.0, -slope.y));
}

float3 WaterReflection(float3 surface, float3 ray, uint2 size, float3 sky) {
    float3 start = mul(float4(surface + float3(0, 2.0, 0), 1.0), View).xyz;
    float3 direction = mul(float4(ray, 0.0), View).xyz;
    float previous = 0.0;
    [loop] for (int i = 0; i < 14; ++i) {
        float t = (float(i) + 1.0) / 14.0;
        float travel = 8.0 + t * t * 420.0;
        float3 samplePoint = start + direction * travel;
        float2 uv;
        if (!ProjectView(samplePoint, uv)) break;
        float sceneDepth = DepthAt(uv, size);
        float gap = -samplePoint.z - sceneDepth;
        if (gap > 0.9 && gap < lerp(10.0, 33.0, t)) {
            float nearT = previous, farT = travel;
            [unroll] for (int j = 0; j < 3; ++j) {
                float mid = (nearT + farT) * 0.5;
                float3 refined = start + direction * mid;
                float2 candidate;
                if (!ProjectView(refined, candidate)) break;
                if (-refined.z > DepthAt(candidate, size)) {
                    farT = mid;
                    uv = candidate;
                } else {
                    nearT = mid;
                }
            }
            float2 screenEdge = min(uv, 1.0 - uv);
            float confidence = smoothstep(0.01, 0.12, min(screenEdge.x, screenEdge.y)) * (0.88 - 0.44 * t);
            float2 footprint = float2(1.6, 0.8) / size;
            float3 color = 0.5 * ToLightSpace(ColorAt(uv, size));
            color += 0.25 * ToLightSpace(ColorAt(uv - footprint, size));
            color += 0.25 * ToLightSpace(ColorAt(uv + footprint, size));
            return lerp(sky, color, confidence);
        }
        previous = travel;
    }
    return sky;
}

float3 ShadeWater(WaterHit water, float3 eye, float3 worldPoint, float2 uv, uint2 size, float3 sun,
                  float sunlight, float3 localLight) {
    float time = SceneData[0].z;
    float3 normal = WaterNormal(water.surface.xz, time);
    float3 toEye = normalize(eye - water.surface);
    float3 ray = reflect(-toEye, normal);
    float facing = saturate(dot(normal, toEye));
    float fresnel = 0.035 + 0.965 * pow(1.0 - facing, 5.0);

    float2 offset = mul(float4(normal.x, 0, normal.z, 0), View).xy * float2(1, -1);
    float2 refracted = uv + offset * (0.11 / (1.0 + water.depth * 0.003));
    if (!InsideScreen(refracted) || DepthAt(refracted, size) < water.depth - 1.5) refracted = uv;
    float3 bottom = ToLightSpace(ColorAt(refracted, size));
    float thickness = max(water.surface.y - worldPoint.y, 0.0);
    float absorption = 1.0 - exp(-0.014 * (12.0 + thickness));
    float3 transmitted = lerp(bottom * float3(0.69, 1.03, 0.98), float3(0.031, 0.195, 0.179), absorption);
    transmitted *= lerp(float3(0.56, 0.70, 0.83), 1.0, sunlight);

    float wave = WaterRipple(water.surface.xz, time);
    float caustic = pow(saturate(0.47 + 0.26 * wave), 5.0);
    transmitted += float3(0.075, 0.155, 0.12) * caustic * exp(-thickness * 0.021) * sunlight;

    float3 sky = lerp(float3(0.11, 0.20, 0.19), float3(0.28, 0.45, 0.54), saturate(ray.y));
    float3 reflected = WaterReflection(water.surface, ray, size, sky);
    float glint = pow(saturate(dot(ray, sun)), 72.0) * 0.44;
    float3 light = lerp(transmitted, reflected, 0.14 + 0.76 * fresnel);
    light += glint * float3(1.0, 0.88, 0.63) * sunlight;
    light += localLight * lerp(0.14, 0.40, fresnel);
    float foam = (1.0 - smoothstep(1.5, 7.0, thickness)) * smoothstep(0.85, 1.75, wave) * 0.10;
    return lerp(light, float3(0.51, 0.68, 0.61), foam);
}
