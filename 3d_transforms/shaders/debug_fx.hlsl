struct view_proj
{
    matrix view;
    matrix proj;
};
ConstantBuffer<view_proj> cb_viewproj : register(b0);

// input layout
struct vertex_in
{
    float3 pos : POSITION;
    float4 color : COLOR;
};

struct vertex_out
{
    float4 hpos : SV_Position;
    float4 color : COLOR;
};

vertex_out VS(vertex_in vs_in)
{
    matrix view_proj = mul(cb_viewproj.view, cb_viewproj.proj);
    vertex_out ps_in;
    ps_in.hpos = float4(vs_in.pos, 1.0f);
    ps_in.hpos = mul(ps_in.hpos, view_proj);

    ps_in.color = vs_in.color;
    return ps_in;
}

float4 PS(vertex_out ps_in) : SV_Target
{
    return ps_in.color;
}