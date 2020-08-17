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
StructuredBuffer<uint> sb_selected_instance_ids : register(t1);
StructuredBuffer<uint> sb_instance_ids : register(t2);

struct instance_ids_size
{
    uint size;
};
ConstantBuffer<instance_ids_size> cb_selected_insts_size : register(b2);

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

float4 instance_mvp(float3 pos, uint inst_id, matrix SRT)
{
    uint current_instanceID = sb_instance_ids[inst_id];
    // convert to homogeneous clip space and apply the render_item's world matrix
    float4 hpos = mul(float4(pos, 1.0f), cb_object.world);

    // instance transform
    matrix inst_world = sb_instance[current_instanceID].world;

    // merge the instance world matrix and object world matrix
    hpos = mul(hpos, inst_world);
    hpos = mul(hpos, SRT);

    // apply the view_proj to both of them
    matrix view_proj = mul(cb_viewproj.view, cb_viewproj.proj);
    return mul(hpos, view_proj);
}

vertex_out VS(vertex_in vs_in, uint id : SV_InstanceID)
{
    vertex_out ps_in;
    
    matrix identity =
    {
        { 1, 0, 0, 0 },
        { 0, 1, 0, 0 },
        { 0, 0, 1, 0 },
        { 0, 0, 0, 1 }
    };
    ps_in.hpos = instance_mvp(vs_in.pos, id, identity);
    ps_in.color = vs_in.color;
    return ps_in;
}

float4 PS(vertex_out ps_in) : SV_Target
{
    return ps_in.color;
}