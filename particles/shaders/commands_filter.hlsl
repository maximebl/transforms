struct indirect_command
{
    double particle_input_uav;
    double particle_output_uav;
    uint3 dispatch_args;
};
StructuredBuffer<indirect_command> in_commands : register(t0);
AppendStructuredBuffer<indirect_command> out_commands : register(u0);

struct command_info
{
   int command_count; 
};
ConstantBuffer<command_info> cmd_info : register(b0);

[numthreads(1, 1, 1)]
void CS(uint3 thread_id : SV_DispatchThreadID)
{
    int index = thread_id.x;
    if (cmd_info.command_count > index)
    {
        out_commands.Append(in_commands[index]);
    }
}