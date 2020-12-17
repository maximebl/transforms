#include "common.hlsl"

struct particle
{
    float3 position;
    float size;
    float3 velocity;
    float age;
};
RWStructuredBuffer<particle> output_particle : register(u0);

#define num_threads_x (512)
groupshared float3 max_positions[num_threads_x];
groupshared float3 min_positions[num_threads_x];

[numthreads(num_threads_x, 1, 1)]
void CS(uint3 thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID)
{
    int group_tid = group_thread_id.x;

    max_positions[group_tid] = output_particle[group_tid].position;
    min_positions[group_tid] = output_particle[group_tid].position;
    GroupMemoryBarrierWithGroupSync();

    int step_size = 1;
    int num_operating_threads = num_threads_x;

    while (num_operating_threads > 0)
    {
        if (group_tid < num_operating_threads)
        {
            int fst = group_tid * step_size * 2;
            int snd = fst + step_size;

            if (fst < num_threads_x && snd < num_threads_x)
            {
                max_positions[fst] = max(max_positions[fst], max_positions[snd]);
                min_positions[fst] = min(min_positions[fst], min_positions[snd]);
            }
        }

        step_size <<= 1; // double the step size
        num_operating_threads >>= 1; // halve the amount of operating threads
    }

    GroupMemoryBarrierWithGroupSync();

    if (group_tid == 0)
    {
        // The result of the reduction is stored at index 0
        float3 v_max = max_positions[0];
        float3 v_min = min_positions[0];

        bounding_box new_bounds;

        float3 center = (v_max + v_min) * 0.5f;
        float3 extents = v_max - center;

        new_bounds.center = mul(float4(center, 1.f), cb_object.model);
        new_bounds.extents = extents;

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
}