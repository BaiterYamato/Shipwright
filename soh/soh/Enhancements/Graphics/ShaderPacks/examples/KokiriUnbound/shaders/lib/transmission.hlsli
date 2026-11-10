#ifndef KOKIRI_TRANSMISSION
#define KOKIRI_TRANSMISSION

cbuffer LinkSpanTransmission : register(b3) {
    float4 TransmissionData[153];
};

bool CrossesGlass(float3 receiver, float3 ray, uint material) {
    uint count = uint(TransmissionData[0].y);
    [loop] for (uint i = 0; i < count; ++i) {
        uint index = 9 + 3 * i;
        float4 a = TransmissionData[index];
        if (uint(a.w) != material) continue;
        float3 e1 = TransmissionData[index + 1].xyz - a.xyz;
        float3 e2 = TransmissionData[index + 2].xyz - a.xyz;
        float3 p = cross(ray, e2);
        float det = dot(e1, p);
        if (abs(det) < 0.0001) continue;
        float3 offset = receiver - a.xyz;
        float u = dot(offset, p) / det;
        if (u < -0.0001 || u > 1.0001) continue;
        float3 q = cross(offset, e1);
        float v = dot(ray, q) / det;
        if (v < -0.0001 || u + v > 1.0001) continue;
        float t = dot(e2, q) / det;
        if (t > 0.01 && t < 0.99) return true;
    }
    return false;
}

float3 TransmittedLight(float3 receiver, float3 normal) {
    if (TransmissionData[0].y < 1.0) return 0.0;
    float3 color = 0.0;
    [loop] for (uint i = 0; i < uint(TransmissionData[0].x); ++i) {
        float4 tint = TransmissionData[1 + 2 * i];
        float4 light = TransmissionData[2 + 2 * i];
        float3 ray = light.xyz - receiver;
        float distance = length(ray);
        if (distance >= light.w || distance < 1.0) continue;
        float3 toLight = ray / distance;
        float incidence = saturate(dot(normal, toLight));
#ifdef KOKIRI_TRANSMISSION_CONE
        incidence *= smoothstep(0.76, 0.97, dot(-toLight, KOKIRI_TRANSMISSION_CONE));
#endif
        if (incidence <= 0.0 || tint.w <= 0.0) continue;

        // A fixed world-space emitter softens the silhouette without screen-space jitter.
        float coverage = 0.0;
        [unroll] for (int j = 0; j < 4; ++j) {
            float3 offset = float3((j & 1) ? 4.0 : -4.0, 0.0, (j & 2) ? 4.0 : -4.0);
            if (CrossesGlass(receiver, ray + offset, i)) coverage += 0.25;
        }
        float attenuation = 1.0 - smoothstep(0.1, 1.0, distance / light.w);
        color += tint.rgb * tint.w * coverage * incidence * attenuation;
    }
    return color;
}

#endif
