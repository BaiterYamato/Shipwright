Texture2D<float4> Bright : register(t0);
SamplerState LinearClamp : register(s0);

float4 main(float4 position : SV_Position, float2 uv : TEXCOORD0) : SV_Target {
    uint width, height;
    Bright.GetDimensions(width, height);
    float2 stepUV = float2(1.0 / width, 0.0);
    float4 color = Bright.Sample(LinearClamp, uv) * 0.227027;
    color += (Bright.Sample(LinearClamp, uv - stepUV * 1.384615) +
              Bright.Sample(LinearClamp, uv + stepUV * 1.384615)) * 0.316216;
    color += (Bright.Sample(LinearClamp, uv - stepUV * 3.230769) +
              Bright.Sample(LinearClamp, uv + stepUV * 3.230769)) * 0.070270;
    return color;
}
