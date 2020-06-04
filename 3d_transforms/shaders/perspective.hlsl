struct view_proj
{
    matrix view;
    matrix proj;
};
ConstantBuffer<view_proj> cb_viewproj : register(b0);

struct object
{
    matrix world;
};
ConstantBuffer<object> cb_object : register(b1);

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
    vertex_out ps_in;
    //matrix mvp = cb_viewproj.proj * (cb_viewproj.view * cb_object.world);
    //ps_in.hpos = mul(mvp, float4(vs_in.pos, 1.0f)); // homogenous clip space
    ps_in.hpos = mul( float4(vs_in.pos, 1.0f), cb_object.world);
    ps_in.color = vs_in.color;
    return ps_in;
}

float4 PS(vertex_out ps_in) : SV_Target
{
    return ps_in.color;
}