#pragma once
#include "common.h"
#include "transform.h"
#include "particle.h"

struct particle_system_gpu
{
    particle_system_gpu(ID3D12Device *device,
                        ID3D12GraphicsCommandList *cmd_list,
                        std::vector<particle::aligned_aos> *particle_data,
                        D3D12_GPU_VIRTUAL_ADDRESS transform_cbv,
                        D3D12_GPU_VIRTUAL_ADDRESS physics_cbv,
                        D3D12_GPU_VIRTUAL_ADDRESS bounds_indices_gpu_va,
                        D3D12_GPU_VIRTUAL_ADDRESS bounds_vertices_gpu_va,
                        UINT max_particle_count);
    ~particle_system_gpu();
    void reset(ID3D12GraphicsCommandList *cmd_list);
    ID3D12Resource *m_output_default;
    ID3D12Resource *m_output_upload;
    ID3D12Resource *m_output_reset;
    ID3D12Resource *m_input_default;
    ID3D12Resource *m_input_upload;
    ID3D12Resource *m_input_reset;
    transform m_transform;
    UINT m_max_particle_count;

    ID3D12Resource *m_indirect_drawing_default;
    ID3D12Resource *m_indirect_drawing_upload;
    ID3D12Resource *m_indirect_drawing_bounds_default;
    ID3D12Resource *m_indirect_drawing_bounds_upload;
    ID3D12Resource *m_indirect_sim_cmds_input_upload;
    ID3D12Resource *m_indirect_sim_cmds_input_default;
    ID3D12Resource *m_indirect_sim_cmds_swap_default;
    ID3D12Resource *m_indirect_sim_cmds_swap_upload;
    ID3D12Resource *m_indirect_bounds_calc_default;
    ID3D12Resource *m_indirect_bounds_calc_upload;
};

struct draw_indirect_command2
{
    D3D12_VERTEX_BUFFER_VIEW vbv;
    D3D12_GPU_VIRTUAL_ADDRESS transform_cbv;
    D3D12_DRAW_ARGUMENTS draw_args;
};

struct simulation_indirect_command
{
    D3D12_GPU_VIRTUAL_ADDRESS particle_output_uav;
    D3D12_GPU_VIRTUAL_ADDRESS particle_input_uav;
    D3D12_GPU_VIRTUAL_ADDRESS physics_cbv;
    float dt_accum;
    int missed_frames;
    D3D12_DISPATCH_ARGUMENTS dispatch_args;
};

struct bounds_draw_indirect_command
{
    D3D12_VERTEX_BUFFER_VIEW vbv;
    D3D12_INDEX_BUFFER_VIEW ibv;
    D3D12_GPU_VIRTUAL_ADDRESS transform_cbv;
    D3D12_DRAW_INDEXED_ARGUMENTS draw_args;
};

struct bounds_calc_indirect_command
{
    D3D12_GPU_VIRTUAL_ADDRESS particle_output_uav;
    D3D12_GPU_VIRTUAL_ADDRESS transform_cbv;
    D3D12_DISPATCH_ARGUMENTS dispatch_args;
};
