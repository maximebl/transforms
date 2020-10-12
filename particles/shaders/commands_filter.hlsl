struct indirect_command
{
    double pass_data_cbv;
    double particle_output_uav;
    double particle_input_uav;
    uint3 dispatch_args;
};
StructuredBuffer<indirect_command> in_commands : register(t1);
RWStructuredBuffer<indirect_command> out_commands : register(u2);

struct command_info
{
    uint command_count;
};
ConstantBuffer<command_info> cmd_info : register(b2);

[numthreads(1, 1, 1)]
void CS(uint3 thread_id : SV_DispatchThreadID)
{
    uint index = thread_id.x;
    if (cmd_info.command_count > index)
    {
        // filter commands...
        out_commands[0] = in_commands[index];
    }
}