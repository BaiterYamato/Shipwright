Texture2D<float4> SceneColor : register(t0);
Texture2D<float4> NearBloom : register(t1);
Texture2D<float4> WideBloom : register(t2);
SamplerState LinearClamp : register(s0);

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    float4 scene = SceneColor.Sample(LinearClamp, uv);
    float3 linearColor = scene.rgb * scene.rgb;
    float3 nearGlow = NearBloom.Sample(LinearClamp, uv).rgb;
    float3 wideGlow = WideBloom.Sample(LinearClamp, uv).rgb;
    return float4(sqrt(saturate(linearColor + nearGlow * 1.30 + wideGlow * 1.65)), scene.a);
}
