#include "common.hlsl"
#include "shader_shared_constants.h"

struct command_info
{
    uint command_count;
};
ConstantBuffer<command_info> cmd_info : register(b2);

// Particle simulation commands
struct sim_indirect_command
{
    double particle_output_uav;
    double particle_input_uav;
    double physics_cbv;
    float dt_accum;
    int missed_frames;
    uint3 dispatch_args;
};
RWStructuredBuffer<sim_indirect_command> in_sim_commands : register(u8);
AppendStructuredBuffer<sim_indirect_command> out_sim_commands : register(u2);

// Particle drawing commands
struct draw_indirect_command
{
    double vertex_buffer;
    uint2 vbv_args;
    double transform;
    uint4 draw_args;
};
StructuredBuffer<draw_indirect_command> in_draw_commands : register(t2);
AppendStructuredBuffer<draw_indirect_command> out_draw_commands : register(u4);

// Bounds drawing commands
struct bounds_draw_indirect_command
{
    double vertex_buffer;
    uint2 vbv_args;
    double index_buffer;
    uint2 ibv_arg;
    double transform;
    uint4 indexed_draw_args;
    uint indexed_draw_args2;
};
StructuredBuffer<bounds_draw_indirect_command> in_bounds_draw_commands : register(t3);
AppendStructuredBuffer<bounds_draw_indirect_command> out_bounds_draw_commands : register(u5);

// Bounds calculation commands
struct bounds_calc_indirect_command
{
    double particle_output_uav;
    double ps_transform;
    uint3 dispatch_args;
};
StructuredBuffer<bounds_calc_indirect_command> in_bounds_calc_commands : register(t4);
AppendStructuredBuffer<bounds_calc_indirect_command> out_bounds_calc_commands : register(u7);

bool is_aabb_visible(float4 planes[6], float4 center, float4 extents)
{
    for (int i = 0; i < 6; i++)
    {
        float4 plane = planes[i];
        plane = mul(plane, transpose(cb_pass.view));

        float r = abs(plane.x * extents.x) + abs(plane.y * extents.y) + abs(plane.z * extents.z);
        float c = dot(plane.xyz, center.xyz) + plane.w;
        if (c <= -r)
        {
            return false;
        }
    }
    return true;
}

[numthreads(max_num_particle_systems, 1, 1)]
void CS(uint3 thread_id : SV_DispatchThreadID)
{
    uint index = thread_id.x;
    if (cmd_info.command_count > index)
    {
        out_bounds_draw_commands.Append(in_bounds_draw_commands[index]);
        out_bounds_calc_commands.Append(in_bounds_calc_commands[index]);

        // filter commands
        bounding_box current_bounds = bounds_reader[index];
        float4 center = current_bounds.center;
        float4 extents = float4(current_bounds.extents, 0.f);

        if (is_aabb_visible(cb_pass.frustum_planes, center, extents))
        {
            out_draw_commands.Append(in_draw_commands[index]);
            out_sim_commands.Append(in_sim_commands[index]);

            in_sim_commands[index].dt_accum = 0.f;
        }
        else
        {
            in_sim_commands[index].missed_frames = (int)floor(in_sim_commands[index].dt_accum / (1.f / 144.f));
            in_sim_commands[index].dt_accum += cb_pass.delta_time;
        }
    }
}