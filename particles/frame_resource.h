#pragma once
#include <gpu_interface.h>

struct pass_data
{
    DirectX::XMFLOAT4X4 view;
    DirectX::XMFLOAT4X4 proj;
};

class frame_cmd
{
public:
    frame_cmd() = default;
    frame_cmd(ID3D12Device *device, size_t frame_index);
    ~frame_cmd();

    size_t m_frame_index = 0;
    ID3D12CommandAllocator *cmd_alloc = nullptr;
    UINT64 fence_value = 0;
};

class frame_resource : public frame_cmd
{
public:
    frame_resource(ID3D12Device *device, size_t frame_index, BYTE* particle_data);
    ~frame_resource();

    BYTE *particle_vb_range = nullptr;
    std::unique_ptr<upload_buffer> cb_passdata_upload = nullptr;
};

//struct frame_resource
//{
//    frame_resource(ID3D12Device *device, size_t frame_index, BYTE* particle_data);
//    ~frame_resource();
//
//    ID3D12CommandAllocator *cmd_alloc = nullptr;
//    size_t frame_index = 0;
//    UINT64 fence_value = 0;
//
//    BYTE *particle_vb_range = nullptr;
//    upload_buffer *cb_passdata_upload = nullptr;
//};
