cbuffer Rect : register(b0)
{
    float4 rect;
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
    result.uv = corner;
    return result;
}

Texture2D<float> glyphs : register(t0);
SamplerState linear_sampler : register(s0);

float4 PSMain(VSOut input) : SV_Target
{
    return float4(1.0f, 1.0f, 1.0f, glyphs.Sample(linear_sampler, input.uv));
}
