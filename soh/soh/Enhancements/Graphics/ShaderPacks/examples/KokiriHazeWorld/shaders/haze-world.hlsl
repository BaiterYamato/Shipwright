Texture2D<float4> SceneColor : register(t0);
Texture2D<float> SceneDepth : register(t1);

cbuffer LinkSpanCamera : register(b0) {
    row_major float4x4 Projection;
    row_major float4x4 View;
    float4 CameraInfo;
};

float ViewDepth(float depth) {
    float ndc = depth * 2.0 - 1.0;
    float divisor = ndc * Projection[2][3] - Projection[2][2];
    if (depth >= 1.0 || abs(divisor) < 0.000001) return CameraInfo.y;
    return clamp((ndc * Projection[3][3] - Projection[3][2]) / divisor, 0.0, CameraInfo.y);
}

float4 main(float4 position : SV_Position) : SV_Target {
    int3 pixel = int3(int2(position.xy), 0);
    float4 scene = SceneColor.Load(pixel);
    float distance = ViewDepth(SceneDepth.Load(pixel));
    float haze = smoothstep(205.0, 990.0, distance) * 0.62;
    return float4(lerp(scene.rgb, float3(0.47, 0.57, 0.55), haze), scene.a);
}
