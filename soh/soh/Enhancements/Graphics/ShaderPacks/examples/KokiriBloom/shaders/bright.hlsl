Texture2D<float4> SceneColor : register(t0);
SamplerState LinearClamp : register(s0);

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float3 color = SceneColor.Sample(LinearClamp, uv).rgb;
    float3 linearColor = color * color;
    float peak = max(max(linearColor.r, linearColor.g), linearColor.b);
    float weight = smoothstep(0.065, 0.45, peak);
    return float4(linearColor * weight, 1.0);
}
