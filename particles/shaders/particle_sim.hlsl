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

#define flt_max 3.402823E+38
groupshared float3 v_min = float3(flt_max, flt_max, flt_max);
groupshared float3 v_max = float3(-flt_max, -flt_max, -flt_max);

#define threads_count 1024
[numthreads(threads_count, 1, 1)]
void CS(uint3 thread_id : SV_DispatchThreadID)
{
    int index = thread_id.x;

    // Initialize random number generator
    rand_seed(index);

    particle p = input_particle[index];

    // Transform to homogenous clip space for the purpose of frustum culling
    //float4 hpos = float4(0.f, 0.f, 0.f, 0.f);
    //matrix view_proj = mul(cb_pass.view, cb_pass.proj);
    //view_proj = mul(cb_object.model, view_proj);
    //hpos = mul(float4(p.position.xyz, 1.f), view_proj);

    // Frustum culling
    // Let's the shader skip calculating the actions if the particle is out of the view frustum
    //float height = p.size * cb_pass.vert_cotangent;
    //float width = height * cb_pass.aspect_ratio;
    //float3 extent = abs(hpos.xyz) - float3(width, height, 0);

    //// Frustum cull by checking of any of xyz are outside [-w, w].
    //float bound = max(max(0.f, extent.x), max(extent.y, extent.z));
    //if (bound > hpos.w)
    //    return;

    // Run actions on the original particle
    float dt = cb_pass.delta_time;

    // Size
    p.size = 1.f;

    // Gravity
    p.velocity += float3(0.f, 0.00f, 0.f) * dt;

    // Move
    p.position += p.velocity * dt;

    // Output the particle with the actions applied to it
    output_particle[index] = p;

    float3 pos = p.position;

    // Calculate the bounding box vertices.
    // 
    // Temporary solution. Ideally we would calculate the min/max in a different dispatch to avoid race conditions.
    // Wait for previous groupshared writes to finish.
    GroupMemoryBarrier();

    // Calculate min vertex of the bounding box
    if (pos.x < v_min.x)
    {
        v_min.x = pos.x;
    }
    if (pos.y < v_min.y)
    {
        v_min.y = pos.y;
    }
    if (pos.z < v_min.z)
    {
        v_min.z = pos.z;
    }

    // Calculate max vertex of the bounding box
    if (pos.x > v_max.x)
    {
        v_max.x = pos.x;
    }
    if (pos.y > v_max.y)
    {
        v_max.y = pos.y;
    }
    if (pos.z > v_max.z)
    {
        v_max.z = pos.z;
    }

    bounding_box new_bounds;

    // Use WithGroupSync() to have a better chance that prior writes will finish before we read. Still not ideal.
    // WithGroupSync() will force all the group threads to finish calculating the min/max above.
    // Still a poor solution because it's still possible that a thread will write over v_min and v_max while we read.
    GroupMemoryBarrierWithGroupSync();
    new_bounds.position[0] = v_min;
    new_bounds.position[1] = float3(v_min.x, v_max.y, v_min.z);
    new_bounds.position[2] = float3(v_min.x, v_min.y, v_max.z);
    new_bounds.position[3] = float3(v_min.x, v_max.y, v_max.z);
    new_bounds.position[4] = float3(v_max.x, v_min.y, v_min.z);
    new_bounds.position[5] = float3(v_max.x, v_max.y, v_min.z);
    new_bounds.position[6] = float3(v_max.x, v_min.y, v_max.z);
    new_bounds.position[7] = v_max;
    bounds.Append(new_bounds);
}