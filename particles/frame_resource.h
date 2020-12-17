#pragma once
#include <gpu_interface.h>
#include "math_helpers.h"
#include "../main/gpu_interface2.h"

struct pass_data
{
    DirectX::XMFLOAT4X4 view = Identity4x4();
    DirectX::XMFLOAT4X4 proj = Identity4x4();
    DirectX::XMFLOAT3 eye_pos;
    float time = 0.f;
    float delta_time = 0.f;
    float aspect_ratio = 0.f;
    float _pad0 = 0.f;
    float _pad1 = 0.f;
    DirectX::XMFLOAT4 frustum_planes[6] = {};
};

struct material_data
{
    DirectX::XMFLOAT4X4 transform = Identity4x4();
    DirectX::XMFLOAT4X4 inv_transform = Identity4x4();
};

struct model_data
{
    DirectX::XMFLOAT4X4 transform = Identity4x4();
};

struct bounding_box
{
    DirectX::XMFLOAT3 extents;
    DirectX::XMFLOAT4 center;
    DirectX::XMFLOAT3 positions[8]; // No need for padding when using arrays
};

struct physics
{
    float drag_coefficients[2] = {1.05f, 1.05f};
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
    frame_resource(ID3D12Device *device, size_t frame_index, BYTE *particle_data, UINT num_transforms);
    ~frame_resource();

    BYTE *particle_vb_range = nullptr;
    std::unique_ptr<upload_buffer2<pass_data>> cb_pass_upload = nullptr;
    std::unique_ptr<upload_buffer> cb_material_upload = nullptr;
    std::unique_ptr<upload_buffer2<model_data>> cb_transforms_upload = nullptr;
    std::unique_ptr<upload_buffer2<position_color>> cb_debug_vertices_upload = nullptr;
    std::unique_ptr<upload_buffer2<physics>> cb_physics = nullptr;
};
