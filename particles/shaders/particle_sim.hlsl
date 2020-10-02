#include "common.hlsl"

// input layout
struct vertex_in
{
    float4 position : POSITION;
    float size : SIZE;
    float4 velocity : VELOCITY;
    float age : AGE;
};

struct particle
{
    float4 position;
    float size;
    float4 velocity;
    float age;
};
RWStructuredBuffer<particle> output_particle : register(u0);
RWStructuredBuffer<particle> reset_particle : register(u1);
RWStructuredBuffer<particle> input_particle : register(u2);

[numthreads(1024, 1, 1)] // Threads per thread group
void CS(uint3 thread_id : SV_DispatchThreadID,
uint3 group_thread_id : SV_GroupThreadID,
uint group_index : SV_GroupIndex,
uint3 group_id : SV_GroupID)
{
    int index = thread_id.x;

    // Initialize random number generator
    rand_seed(index);

    particle p = input_particle[index];

    // Transform to homogenous clip space for the purpose of frustum culling
    float4 hpos = float4(0.f, 0.f, 0.f, 0.f);
    matrix view_proj = mul(cb_pass.view, cb_pass.proj);
    hpos = mul(float4(p.position.xyz, 1.f), view_proj);

    // Frustum culling
    // Let's the shader skip calculating the actions if the particle is out of the view frustum
    float height = p.size * cb_pass.vert_cotangent;
    float width = height * cb_pass.aspect_ratio;
    float3 extent = abs(hpos.xyz) - float3(width, height, 0);

    float unknown = max(max(0.f, extent.x), max(extent.y, extent.z));
    if (unknown > hpos.w)
        return;

    // Run actions on the original particle before the transform to homogenous clip space
    float dt = cb_pass.delta_time;

    // Gravity
    p.velocity += float4(0.f, 0.01f, 0.f, 1.f) * dt;

    // Move
    p.position += p.velocity * dt;

    // Keep the view space particle with the actions applied to it
    reset_particle[index] = p;

    // Output the particle with the actions applied to it, in homogeneous clip space
    p.position = mul(float4(p.position.xyz, 1.f), view_proj);
    output_particle[index] = p;
}

// Point rendering without projection
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
    vertex_out ps_in;
    ps_in.hpos = vs_in.position;
    return ps_in;
}

pixel_out PS(vertex_out ps_in)
{
    pixel_out ps_out;
    ps_out.color = float4(1.f, 0.f, 0.f, 1.f);
    return ps_out;
}