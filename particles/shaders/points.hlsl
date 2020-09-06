#include "common.hlsl"

struct vertex_out
{
    float4 hpos : SV_Position;
};

struct pixel_out
{
    float4 color : SV_Target;
};

vertex_out VS(vertex_in vs_in)
{
    matrix view_proj = mul(cb_pass.view, cb_pass.proj);
    vertex_out ps_in;
    ps_in.hpos = mul(float4(vs_in.position, 1.f), view_proj);

    return ps_in;
}

pixel_out PS(vertex_out ps_in)
{
    pixel_out ps_out;
    ps_out.color = float4(1.f, 0.f, 0.f, 1.f);
    return ps_out;
}