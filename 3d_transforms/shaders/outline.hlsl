#include "perspective.hlsl"

struct PixelShaderInput
{
    float4 Position : SV_Position;
    float4 Color : COLOR;
};

float4 PS_outline(PixelShaderInput IN) : SV_Target
{
    return float4(0.16, 0.29f, 0.48, 1.f);
}
vertex_out VS_outline(vertex_in vs_in, uint id : SV_InstanceID)
{
    vertex_out ps_in;
    matrix identity =
    {
        { 1.03, 0, 0, 0 },
        { 0, 1.03, 0, 0 },
        { 0, 0, 1.03, 0 },
        { 0, 0, 0,   1.f }
    };
    ps_in.hpos = instance_mvp(vs_in.pos, id, identity);
    ps_in.color = vs_in.color;
    return ps_in;
}
