#include "pch.h"
#include "particle_system_gpu.h"
#include "gpu_interface.h"

particle_system_gpu::particle_system_gpu(ID3D12Device *device,
                                         ID3D12GraphicsCommandList *cmd_list,
                                         std::vector<particle::aligned_aos> *particle_data,
                                         D3D12_GPU_VIRTUAL_ADDRESS transform,
                                         UINT max_particle_count,
                                         UINT64 bounds_indices_gpu_va,
                                         UINT64 bounds_vertices_gpu_va)
    : m_max_particle_count(max_particle_count)
{
    // Create the particle buffers
    size_t particle_count = particle_data->size();
    size_t buffer_size = particle_count * particle::byte_size;

    create_default_buffer(device, cmd_list, particle_data->data(), buffer_size,
                          &m_output_upload, &m_output_default, "particle_output_data", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    create_default_buffer(device, cmd_list, particle_data->data(), buffer_size,
                          &m_input_upload, &m_input_default, "particle_input_data", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    // Create indirect drawing command objects for particles
    D3D12_DRAW_ARGUMENTS draw_args = {};
    draw_args.InstanceCount = 1;
    draw_args.VertexCountPerInstance = m_max_particle_count;
    draw_args.StartInstanceLocation = 0;
    draw_args.StartVertexLocation = 0;

    draw_indirect_command2 indirect_cmd = {};
    indirect_cmd.vbv.BufferLocation = m_output_default->GetGPUVirtualAddress();
    indirect_cmd.vbv.SizeInBytes = m_max_particle_count * particle::byte_size;
    indirect_cmd.vbv.StrideInBytes = particle::byte_size;
    indirect_cmd.transform = transform;
    indirect_cmd.draw_args = draw_args;

    size_t indirect_arg_buffer_size = sizeof(draw_indirect_command2);

    create_default_buffer(device, cmd_list,
                          (void *)&indirect_cmd, indirect_arg_buffer_size,
                          &m_indirect_drawing_upload, &m_indirect_drawing_default, "particle_draw_args");

    // Create the indirect indexed drawing command objects for particle bounds
    indirect_arg_buffer_size = sizeof(bounds_draw_indirect_command);
    bounds_draw_indirect_command indirect_bounds_draw_cmd = {};
    indirect_bounds_draw_cmd.ibv.BufferLocation = bounds_indices_gpu_va;
    indirect_bounds_draw_cmd.ibv.Format = DXGI_FORMAT_R16_UINT;
    indirect_bounds_draw_cmd.ibv.SizeInBytes = 24 * sizeof(UINT16);

    indirect_bounds_draw_cmd.vbv.BufferLocation = bounds_vertices_gpu_va;
    indirect_bounds_draw_cmd.vbv.SizeInBytes = sizeof(DirectX::XMFLOAT3) * 8;
    indirect_bounds_draw_cmd.vbv.StrideInBytes = sizeof(DirectX::XMFLOAT3);

    indirect_bounds_draw_cmd.transform = transform;

    indirect_bounds_draw_cmd.draw_args.BaseVertexLocation = 0;
    indirect_bounds_draw_cmd.draw_args.IndexCountPerInstance = 24;
    indirect_bounds_draw_cmd.draw_args.InstanceCount = 1;
    indirect_bounds_draw_cmd.draw_args.StartIndexLocation = 0;
    indirect_bounds_draw_cmd.draw_args.StartInstanceLocation = 0;

    create_default_buffer(device, cmd_list,
                          (void *)&indirect_bounds_draw_cmd, indirect_arg_buffer_size,
                          &m_indirect_drawing_bounds_upload, &m_indirect_drawing_bounds_default, "particle_bounds_draw_args");

    // Create the indirect simulation command objects for particles
    D3D12_DISPATCH_ARGUMENTS dispatch_args = {1, 1, 1};
    simulation_indirect_command simulation_cmd = {};
    simulation_cmd.dispatch_args = dispatch_args;
    simulation_cmd.particle_input_uav = m_input_default->GetGPUVirtualAddress();
    simulation_cmd.particle_output_uav = m_output_default->GetGPUVirtualAddress();

    indirect_arg_buffer_size = sizeof(simulation_indirect_command);

    create_default_buffer(device, cmd_list,
                          (void *)&simulation_cmd, indirect_arg_buffer_size,
                          &m_indirect_sim_cmds_input_upload, &m_indirect_sim_cmds_input_default, "sim_cmds_input_args");

    simulation_cmd.particle_input_uav = m_output_default->GetGPUVirtualAddress();
    simulation_cmd.particle_output_uav = m_input_default->GetGPUVirtualAddress();
    create_default_buffer(device, cmd_list,
                          (void *)&simulation_cmd, indirect_arg_buffer_size,
                          &m_indirect_sim_cmds_swap_upload, &m_indirect_sim_cmds_swap_default, "sim_cmds_swapped_args");
}

particle_system_gpu::~particle_system_gpu()
{
}
