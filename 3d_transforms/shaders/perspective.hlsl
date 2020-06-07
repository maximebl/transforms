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

struct instance
{
    matrix world;
};
StructuredBuffer<instance> sb_instance : register(t0);

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

vertex_out VS(vertex_in vs_in, uint id : SV_InstanceID)
{
    matrix view_proj = mul(cb_viewproj.view, cb_viewproj.proj);

    vertex_out ps_in;
    // convert to homogeneous clip space and apply the render_item's world matrix
    ps_in.hpos = mul(float4(vs_in.pos, 1.0f), cb_object.world);

    // instance transform
    matrix inst_world = sb_instance[id].world;
    // merge the instance world matrix and object world matrix
    ps_in.hpos = mul(ps_in.hpos, inst_world);
    // apply the view_proj to both of them
    ps_in.hpos = mul(ps_in.hpos, view_proj);

    ps_in.color = vs_in.color;
    return ps_in;
}

float4 PS(vertex_out ps_in) : SV_Target
{
    return ps_in.color;
}