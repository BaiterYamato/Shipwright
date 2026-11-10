Texture2D<float4> SceneColor : register(t0);

float4 main(float4 position : SV_Position) : SV_Target {
    uint width, height;
    SceneColor.GetDimensions(width, height);
    int2 pixel = int2(position.xy);
    int2 edge = int2(width, height) - 1;
    float4 scene = SceneColor.Load(int3(pixel, 0));
    float luma = dot(scene.rgb, float3(0.2126, 0.7152, 0.0722));

    float3 glow = 0.0;
    int radius = max(1, int(height / 240));
    const int2 offsets[4] = { int2(-1, 0), int2(1, 0), int2(0, -1), int2(0, 1) };
    [unroll] for (int i = 0; i < 4; ++i) {
        float3 sampleColor = SceneColor.Load(int3(clamp(pixel + offsets[i] * radius, 0, edge), 0)).rgb;
        glow += max(sampleColor - 0.72, 0.0);
    }

    float3 color = lerp(float3(luma, luma, luma), scene.rgb, 1.14);
    float3 tint = lerp(float3(0.85, 0.96, 1.16), float3(1.22, 1.08, 0.87), smoothstep(0.16, 0.82, luma));
    float2 uv = position.xy / float2(width, height) - 0.5;
    float vignette = 1.0 - 0.18 * smoothstep(0.08, 0.50, dot(uv, uv));
    color = (color * tint + glow * float3(0.17, 0.13, 0.09)) * vignette;
    return float4(pow(saturate(color), 0.81), scene.a);
}
