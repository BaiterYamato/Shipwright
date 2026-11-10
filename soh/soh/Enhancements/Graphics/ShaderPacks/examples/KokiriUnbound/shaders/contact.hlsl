#include "lib/lighting.hlsli"
#if KOKIRI_STATIC_SUN_SHADOW
#include "/lib/sunshadow.hlsli"
#endif

float4 main(float4 position : SV_Position) : SV_Target {
    if (!UnboundArea(uint(SceneData[0].x))) return 0.0;
    uint2 size;
    SceneDepth.GetDimensions(size.x, size.y);
    float2 uv = position.xy / ((size + 1u) / 2u);
    float distance = DepthAt(uv, size);
    float3 viewPoint = ViewPosition(uv, distance);
    float3x3 inverseView = ViewToWorld();
    float3 eye = mul(-View[3].xyz, inverseView);
    float3 surface = eye + mul(viewPoint, inverseView);
    float3 normal = SurfaceNormal(surface, eye, distance);
    float shadow = ContactShadow(viewPoint, normal, SceneSun(), distance, size);
    float worldShadow = 0.0;
#if KOKIRI_STATIC_SUN_SHADOW
    if (distance < CameraInfo.y - 1.0) {
        worldShadow = SunOcclusion(surface, normal);
        shadow = max(shadow, worldShadow * SunDaylight() * 0.88);
    }
#endif
    return float4(shadow, distance, worldShadow, 1.0);
}
