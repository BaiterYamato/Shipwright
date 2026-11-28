Texture2D<float4> NearBloom : register(t0);
SamplerState LinearClamp : register(s0);

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    return NearBloom.Sample(LinearClamp, uv);
}
