Texture2D<float4> SceneColor : register(t0);
Texture2D<float> SceneDepth : register(t1);

float4 main(float4 position : SV_Position) : SV_Target {
    int3 pixel = int3(int2(position.xy), 0);
    float4 scene = SceneColor.Load(pixel);
    float depth = SceneDepth.Load(pixel);
    // This curve operates in device depth, so it does not assume a particular camera projection.
    float haze = smoothstep(0.969, 0.999, depth) * 0.62;
    float3 hazeColor = float3(0.47, 0.57, 0.55);
    return float4(lerp(scene.rgb, hazeColor, haze), scene.a);
}
