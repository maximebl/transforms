#include "pch.h"
#include "frame_resource.h"

frame_cmd::~frame_cmd()
{
    safe_release(cmd_alloc);
}

frame_resource::frame_resource(ID3D12Device *device, size_t frame_index, BYTE *particle_data, UINT num_transforms)
{
    check_hr(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&cmd_alloc)));
    NAME_D3D12_OBJECT_INDEXED(cmd_alloc, (UINT)frame_index);

    particle_vb_range = particle_data;

    //cb_pass_upload = std::make_unique<upload_buffer>(device, 1, sizeof(pass_data), true, "pass_data");
    cb_pass_upload = std::make_unique<upload_buffer2<pass_data>>(device, sizeof(pass_data), true);
    NAME_D3D12_OBJECT_INDEXED(cb_pass_upload->m_upload, (UINT)frame_index);

    cb_material_upload = std::make_unique<upload_buffer>(device, 1, sizeof(material_data), true, "material_data");

    cb_transforms_upload = std::make_unique<upload_buffer2<model_data>>(device, num_transforms, true);
    NAME_D3D12_OBJECT_INDEXED(cb_transforms_upload->m_upload, (UINT)frame_index);

    cb_debug_vertices_upload = std::make_unique<upload_buffer2<position_color>>(device, sizeof(position_color) * MAX_DEBUG_VERTICES);
    NAME_D3D12_OBJECT_INDEXED(cb_debug_vertices_upload->m_upload, (UINT)frame_index);

    cb_physics = std::make_unique<upload_buffer2<physics>>(device, sizeof(physics), true);
    NAME_D3D12_OBJECT_INDEXED(cb_physics->m_upload, (UINT)frame_index);
}

frame_resource::~frame_resource()
{
}
