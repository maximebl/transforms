#include "common.hlsl"

struct material
{
    matrix transform;
    matrix inv_transform;
};
ConstantBuffer<material> cb_material : register(b1);

Texture2D fire_texture : register(t0);
SamplerState linear_wrap : register(s0);

struct vertex_out
{
    float3 pos : POSITION;
    float size : SIZE;
};

struct geo_out
{
    float4 hpos : SV_Position;
    float2 tex_coord : TEXCOORD;
    float2 panning_texcoord : PANNING_TEXCOORD;
    float2 inv_panning_texcoord : INV_PANNING_TEXCOORD;
};

struct pixel_out
{
    float4 color : SV_Target;
};

static uint rng_state;

void rand_seed(int seed)
{
    // wang hash from: http://www.burtleburtle.net/bob/hash/integer.html
    seed = (seed ^ 61) ^ (seed >> 16);
    seed *= 9;
    seed = seed ^ (seed >> 4);
    seed *= 0x27d4eb2d;
    seed = seed ^ (seed >> 15);

    // set starting seed
    rng_state = seed;
}

float rand()
{
    // LCG values from Numerical Recipes
    rng_state = 1664525 * rng_state + 1013904223;

    // Generate a random float in [0, 1)...
    return float(rng_state) * (1.0 / 4294967296.0);
}

vertex_out VS(uint vertex_id : SV_VertexID ,vertex_in vs_in)
{
    rand_seed(vs_in.position.x);

    vertex_out gs_in;
    gs_in.pos = vs_in.position;
    gs_in.size = vs_in.size;
    return gs_in;
}

#define num_billboard_verts 4
[maxvertexcount(num_billboard_verts)]
void GS(point vertex_out gs_in[1], inout TriangleStream<geo_out> stream)
{
    vertex_out current_point = gs_in[0];

    //
    // Compute the local coordinate system of the sprite relative to world space.
    //
    float3 up = float3(0.0f, 1.0f, 0.0f);

    // Get the vector from the billboard's position to the eye
    float3 forward = normalize(cb_pass.eye_pos - current_point.pos);

    // project to the XZ plane
    forward.y = 0.f;
    float3 right = cross(up, forward);

    //
    // Compute quad in world space
    //
    float half_width = current_point.size;
    float half_height = half_width;

    float3 scaled_right = half_width * right;
    float3 scaled_up = half_height * up;

    float2 tex_coords[4] =
    {
        // One texture coordinate for each corner of the billboard
        float2(0.0f, 1.0f), // Bottom left
		float2(0.0f, 0.0f), // Top left
		float2(1.0f, 1.0f), // Bottom right
		float2(1.0f, 0.0f) // Top right
    };

    matrix view_proj = mul(cb_pass.view, cb_pass.proj);
    geo_out gs_out;

    // The reason we're not using an array and a loop is due to a bug in code gen when using GPU-based validation
    // in older versions of Windows 10.

    // Bottom left vertex
    float4 new_bl_pos = float4((current_point.pos + scaled_right) - scaled_up, 1.f);
    gs_out.hpos = mul(new_bl_pos, view_proj);

    float4 bl_texc = float4(tex_coords[0], 0.f, 1.f);
    gs_out.panning_texcoord = mul(bl_texc, cb_material.transform).xy;
    gs_out.inv_panning_texcoord = mul(bl_texc, cb_material.inv_transform).xy;
    gs_out.tex_coord = tex_coords[0];
    stream.Append(gs_out);

    // Top left vertex
    float4 new_tl_pos = float4((current_point.pos + scaled_right) + scaled_up, 1.f);
    gs_out.hpos = mul(new_tl_pos, view_proj);

    float4 tl_texc = float4(tex_coords[1], 0.f, 1.f);
    gs_out.panning_texcoord = mul(tl_texc, cb_material.transform).xy;
    gs_out.inv_panning_texcoord = mul(tl_texc, cb_material.inv_transform).xy;
    gs_out.tex_coord = tex_coords[1];
    stream.Append(gs_out);

    // Bottom right vertex
    float4 new_br_pos = float4((current_point.pos - scaled_right) - scaled_up, 1.f);
    gs_out.hpos = mul(new_br_pos, view_proj);

    float4 br_texc = float4(tex_coords[2], 0.f, 1.f);
    gs_out.panning_texcoord = mul(br_texc, cb_material.transform).xy;
    gs_out.inv_panning_texcoord = mul(br_texc, cb_material.inv_transform).xy;
    gs_out.tex_coord = tex_coords[2];
    stream.Append(gs_out);

    // Top right vertex
    float4 new_tr_pos = float4((current_point.pos - scaled_right) + scaled_up, 1.f);
    gs_out.hpos = mul(new_tr_pos, view_proj);

    float4 tr_texc = float4(tex_coords[3], 0.f, 1.f);
    gs_out.panning_texcoord = mul(tr_texc, cb_material.transform).xy;
    gs_out.inv_panning_texcoord = mul(tr_texc, cb_material.inv_transform).xy;
    gs_out.tex_coord = tex_coords[3];
    stream.Append(gs_out);
}

pixel_out PS(geo_out ps_in)
{
    float4 sampled_color = fire_texture.Sample(linear_wrap, ps_in.panning_texcoord);
    float panning_alpha = sampled_color.a;

    float4 inv_sampled_color = fire_texture.Sample(linear_wrap, ps_in.inv_panning_texcoord);
    float inv_panning_alpha = inv_sampled_color.a;

    // Premultiply alpha for additive blending
    sampled_color.rgb *= panning_alpha;
    inv_sampled_color.rgb *= inv_panning_alpha;

    sampled_color.rgb *= inv_sampled_color.rgb;

    // Use the original texture alpha as a stencil to cover the billboard square edges
    float original_alpha = fire_texture.Sample(linear_wrap, ps_in.tex_coord).a;
    float stencil = (original_alpha * panning_alpha);
    sampled_color.rgb *= stencil;

    // Brighten the final result
    sampled_color.rgb *= 50.f;

    pixel_out ps_out;

    //ps_out.color = sampled_color;
    ps_out.color = float4(1.f,0.f,0.f,1.f);
    return ps_out;
}