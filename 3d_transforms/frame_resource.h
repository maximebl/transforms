#pragma once
#include <common.h>
#include <gpu_interface.h>

struct object_data
{
    DirectX::XMFLOAT4X4 world;
};

struct instance_data
{
    DirectX::XMFLOAT4X4 world;
};

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
    frame_resource() = default;
    frame_resource(ID3D12Device *device, size_t frame_index, size_t element_count, size_t instance_count);
    ~frame_resource();
    upload_buffer *cb_objconstant_upload = nullptr;
    upload_buffer *sb_instancedata_upload = nullptr;
    upload_buffer *sb_instanceIDs_upload = nullptr;
    upload_buffer *sb_selected_instanceIDs_upload = nullptr;
    upload_buffer *cb_pass_upload = nullptr;
    size_t cb_objconstants_size = 0;
};

struct instance
{
    std::string name;
    bool is_selected = false;
    instance_data shader_data;
    float translation[3] = {};
    float scale[3] = {};
    float right_angle = 0.f;
    float up_angle = 0.f;
    float forward_angle = 0.f;
    DirectX::BoundingBox bounds;
};

struct render_item
{
    render_item() = default;

    std::string name;
    bool is_selected = false;
    DirectX::XMFLOAT4X4 world;
    int cb_index = -1;
    D3D12_PRIMITIVE_TOPOLOGY topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

    mesh meshes;
    std::vector<instance> instances;

    UINT index_count = 0;
    UINT vertex_count = 0;
    UINT start_index_location = 0;
    int base_vertex_location = 0;
};