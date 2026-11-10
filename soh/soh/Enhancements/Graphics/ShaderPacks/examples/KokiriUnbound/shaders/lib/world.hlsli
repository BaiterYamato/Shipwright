#ifndef KOKIRI_WORLD
#define KOKIRI_WORLD

Texture2D<float4> SceneColor : register(t0);
Texture2D<float> SceneDepth : register(t1);

cbuffer LinkSpanCamera : register(b0) {
    row_major float4x4 Projection;
    row_major float4x4 View;
    float4 CameraInfo;
};

cbuffer LinkSpanScene : register(b1) {
    float4 SceneData[36];
};

#if KOKIRI_STATIC_SUN_SHADOW
cbuffer LinkSpanSunShadow : register(b2) {
    float4 SunShadowData[4];
};
#endif

bool UnboundArea(uint area) {
    return area == 0x55u || area == 0x5Bu || area == 0x53u || area == 0x07u || area == 0x57u;
}

float ViewDepth(float depth) {
    float ndc = depth * 2.0 - 1.0;
    float divisor = ndc * Projection[2][3] - Projection[2][2];
    if (depth >= 1.0 || abs(divisor) < 0.000001) return CameraInfo.y;
    return clamp((ndc * Projection[3][3] - Projection[3][2]) / divisor, 0.0, CameraInfo.y);
}

float3 ViewPosition(float2 uv, float distance) {
    float z = -distance;
    float w = z * Projection[2][3] + Projection[3][3];
    float2 ndc = uv * float2(2.0, -2.0) + float2(-1.0, 1.0);
    float2 xy = ndc * w - z * Projection[2].xy - Projection[3].xy;
    return float3(xy / float2(Projection[0][0], Projection[1][1]), z);
}

// The host interpolates the actual view matrix. Invert its linear part even
// when the interpolated axes are no longer exactly orthogonal.
float3x3 ViewToWorld() {
    float3 a = View[0].xyz;
    float3 b = View[1].xyz;
    float3 c = View[2].xyz;
    float3 x = cross(b, c);
    float3 y = cross(c, a);
    float3 z = cross(a, b);
    float det = dot(a, x);
    return transpose(float3x3(x, y, z)) / (abs(det) > 0.00001 ? det : 1.0);
}

bool InsideScreen(float2 uv) {
    return all(uv > 0.0) && all(uv < 1.0);
}

bool ProjectView(float3 surface, out float2 uv) {
    float4 clip = mul(float4(surface, 1.0), Projection);
    uv = clip.xy / max(clip.w, 0.0001) * float2(0.5, -0.5) + 0.5;
    return clip.w > 0.0 && InsideScreen(uv);
}

float DepthAt(float2 uv, uint2 size) {
    return ViewDepth(SceneDepth.Load(int3(min(uint2(saturate(uv) * size), size - 1), 0)));
}

float3 ColorAt(float2 uv, uint2 size) {
    float2 pixel = clamp(uv * size - 0.5, 0.0, float2(size) - 1.0);
    int2 base = int2(pixel);
    int2 next = min(base + 1, int2(size) - 1);
    float2 t = frac(pixel);
    float3 a = SceneColor.Load(int3(base, 0)).rgb;
    float3 b = SceneColor.Load(int3(next.x, base.y, 0)).rgb;
    float3 c = SceneColor.Load(int3(base.x, next.y, 0)).rgb;
    float3 d = SceneColor.Load(int3(next, 0)).rgb;
    return lerp(lerp(a, b, t.x), lerp(c, d, t.x), t.y);
}

float3 SurfaceNormal(float3 surface, float3 eye, float distance) {
    float3 dx = ddx(surface), dy = ddy(surface);
    float3 crossProduct = cross(dy, dx);
    float lengthSq = dot(crossProduct, crossProduct);
    float maxStep = max(10.0, distance * 0.028);
    if (lengthSq < 0.000001 || max(dot(dx, dx), dot(dy, dy)) > maxStep * maxStep) return float3(0, 1, 0);
    float3 normal = crossProduct * rsqrt(lengthSq);
    return dot(normal, eye - surface) >= 0.0 ? normal : -normal;
}

float3 SceneSun() {
#if KOKIRI_STATIC_SUN_SHADOW
    if (SunShadowData[3].x > 0.5) return SunShadowData[2].xyz;
#endif
    float3 sun = SceneData[1].xyz;
    if (dot(sun, sun) < 1.0) return normalize(float3(-0.42, 0.68, -0.58));
    return normalize(float3(sun.x, max(abs(sun.y) * 0.7, length(sun) * 0.32), sun.z));
}

// The framebuffer carries the game's display-referred palette.
float3 ToLightSpace(float3 color) { return color * color; }
float3 ToDisplay(float3 color) { return sqrt(max(color, 0.0)); }

#endif
