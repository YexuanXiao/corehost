cbuffer Rect : register(b0)
{
    float4 rect;
    float4 uv_rect;
    float4 tint;
};

cbuffer Correction : register(b1)
{
    float4 gamma_ratios;
    float grayscale_enhanced_contrast;
    float3 correction_padding;
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
    const float3 foreground = tint.rgb;
    const float lightness = dot(foreground, float3(0.30f, 0.59f, 0.11f));
    const float contrast = grayscale_enhanced_contrast * saturate(lightness * -4.0f + 3.0f);
    const float contrasted = alpha * (contrast + 1.0f) / (alpha * contrast + 1.0f);
    const float intensity = dot(foreground, float3(0.25f, 0.5f, 0.25f));
    const float corrected = contrasted + contrasted * (1.0f - contrasted) *
        ((gamma_ratios.x * intensity + gamma_ratios.y) * contrasted +
         (gamma_ratios.z * intensity + gamma_ratios.w));
    return float4(tint.rgb, tint.a * saturate(corrected));
}
