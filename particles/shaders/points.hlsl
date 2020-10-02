#include "common.hlsl"

// input layout
struct vertex_in
{
    float4 position : POSITION;
    float size : SIZE;
    float4 velocity : VELOCITY;
    float age : AGE;
};

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
    //ps_in.hpos = mul(vs_in.position, view_proj);
    ps_in.hpos = vs_in.position;

    return ps_in;
}

pixel_out PS(vertex_out ps_in)
{
    pixel_out ps_out;
    ps_out.color = float4(1.f, 0.f, 0.f, 1.f);
    return ps_out;
}