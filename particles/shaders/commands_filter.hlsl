#include "common.hlsl"
#include "shader_shared_constants.h"

struct command_info
{
    uint command_count;
};

// Particle simulation commands
struct sim_indirect_command
{
    double particle_output_uav;
    double particle_input_uav;
    uint3 dispatch_args;
};
StructuredBuffer<sim_indirect_command> in_sim_commands : register(t1);
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

ConstantBuffer<command_info> cmd_info : register(b2);

[numthreads(max_num_particle_systems, 1, 1)]
void CS(uint3 thread_id : SV_DispatchThreadID)
{
    uint index = thread_id.x;
    if (cmd_info.command_count > index)
    {
        // filter commands...
        out_draw_commands.Append(in_draw_commands[index]);
        out_sim_commands.Append(in_sim_commands[index]);
        out_bounds_draw_commands.Append(in_bounds_draw_commands[index]);
    }
}