struct view_proj
{
    matrix view;
    matrix proj;
};
ConstantBuffer<view_proj> cb_viewproj : register(b0);

// input layout
struct vertex_in
{
    float3 position : POSITION;
    float4 color : COLOR;
    float3 velocity : VELOCITY;
    float age : AGE;
};

struct vertex_out
{
    float4 hpos : SV_Position;
    float4 color : COLOR;
};

struct geo_out
{
    float4 hpos : SV_Position;
    float4 color : COLOR;
    float2 tex_coord : TEXCOORD;
};

struct pixel_out
{
    float4 color : SV_Target;
};

vertex_out VS(vertex_in vs_in)
{
    matrix view_proj = mul(cb_viewproj.view, cb_viewproj.proj);
    vertex_out ps_in;
    ps_in.hpos = mul(float4(vs_in.position, 1.f), view_proj);

    ps_in.color = vs_in.color;
    return ps_in;
}

geo_out GS(vertex_out gs_in)
{
}

pixel_out PS(vertex_out ps_in)
{
    pixel_out ps_out;
    ps_out.color = float4(1.f, 0.f, 0.f, 1.f);
    return ps_out;
}