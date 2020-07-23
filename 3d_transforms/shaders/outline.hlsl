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
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, 1, 0 },
        { 0, 0, 0, 1 }
    };
    matrix outline_scale =
    {
        { 1.2f, 0, 0, 0 },
        { 0, 1.2f, 0, 0 },
        { 0, 0, 1.2f, 0 },
        { 0, 0, 0, 1 }
    };

    for (uint i = 0; i < cb_selected_insts_size.size; i++)
    {
        if (sb_selected_instance_ids[i] == sb_instance_ids[id])
        {
            identity = mul(identity, outline_scale);
        }
    }

    ps_in.hpos = instance_mvp(vs_in.pos, id, identity);
    ps_in.color = vs_in.color;
    return ps_in;
}
