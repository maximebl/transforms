#include "common.hlsl"

// input layout
struct vertex_in
{
    float3 position : POSITION;
    float size : SIZE;
    float3 velocity : VELOCITY;
    float age : AGE;
};

struct particle
{
    float3 position;
    float size;
    float3 velocity;
    float age;
};
RWStructuredBuffer<particle> output_particle : register(u0);
RWStructuredBuffer<particle> input_particle : register(u1);

[numthreads(1024, 1, 1)]
void CS(uint3 thread_id : SV_DispatchThreadID)
{
    int index = thread_id.x;

    // Initialize random number generator
    rand_seed(index);

    particle p = input_particle[index];

    //// Transform to homogenous clip space for the purpose of frustum culling
    //float4 hpos = float4(0.f, 0.f, 0.f, 0.f);
    //matrix view_proj = mul(cb_pass.view, cb_pass.proj);
    ////view_proj = mul(view_proj, cb_object.model);
    //hpos = mul(float4(p.position.xyz, 1.f), view_proj);

    //// Frustum culling
    //// Let's the shader skip calculating the actions if the particle is out of the view frustum
    //float height = p.size * cb_pass.vert_cotangent;
    //float width = height * cb_pass.aspect_ratio;
    //float3 extent = abs(hpos.xyz) - float3(width, height, 0);

    //// Frustum cull by checking of any of xyz are outside [-w, w].
    //float unknown = max(max(0.f, extent.x), max(extent.y, extent.z));
    //if (unknown > hpos.w)
    //    return;

    // Run actions on the original particle before the transform to homogenous clip space
    float dt = cb_pass.delta_time;

    // Size
    p.size = 1.f;

    // Gravity
    p.velocity += float3(0.f, 0.0f, 0.f) * dt;

    // Move
    p.position += p.velocity * dt;

    // Output the particle with the actions applied to it, in homogeneous clip space
    output_particle[index] = p;
}