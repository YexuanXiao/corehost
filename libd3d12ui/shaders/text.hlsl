cbuffer Rect : register(b0)
{
    float4 rect;
    float4 uv_rect;
    float4 tint;
};

struct VSOut
{
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint vertex : SV_VertexID)
{
    float2 corners[4] = {float2(0, 0), float2(1, 0), float2(0, 1), float2(1, 1)};
    float2 corner = corners[vertex];

    VSOut result;
    result.position = float4(rect.x + corner.x * rect.z, rect.y + corner.y * rect.w, 0.0f, 1.0f);
    result.uv = float2(uv_rect.x + corner.x * (uv_rect.z - uv_rect.x),
                       uv_rect.y + corner.y * (uv_rect.w - uv_rect.y));
    return result;
}

Texture2D<float> glyphs : register(t0);
SamplerState linear_sampler : register(s0);

float4 PSMain(VSOut input) : SV_Target
{
    const float alpha = glyphs.Sample(linear_sampler, input.uv);
    return float4(tint.rgb, tint.a * alpha);
}
