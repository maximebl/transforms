#pragma once
#include "../common/common.h"
#include "../common/gpu_interface.h"
#include <unordered_map>

using namespace DirectX;

struct object_cb
{
    DirectX::XMFLOAT4X4 world;
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
    frame_resource() = default;
    frame_resource(ID3D12Device *device, size_t frame_index, UINT instance_count);
    ~frame_resource();
    upload_buffer *cb_objconstant_upload = nullptr;
    UINT cb_objconstants_size = 0;
};

struct render_item
{
    render_item() = default;

    mesh *meshes;
    XMFLOAT4X4 world;

    UINT cb_index = -1;
    int num_frames_dirty = NUM_BACK_BUFFERS;
    D3D12_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    UINT index_count = 0;
    UINT start_index_location = 0;
    int base_vertex_location = 0;
};