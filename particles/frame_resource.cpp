#include "pch.h"
#include "frame_resource.h"

frame_cmd::~frame_cmd()
{
    safe_release(cmd_alloc);
}

frame_resource::frame_resource(ID3D12Device *device, size_t frame_index, BYTE *particle_data)
{
    check_hr(device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&cmd_alloc)));
    NAME_D3D12_OBJECT_INDEXED(cmd_alloc, (UINT)frame_index);

    particle_vb_range = particle_data;

    cb_pass_upload = std::make_unique<upload_buffer>(device, 1, sizeof(pass_data), true, "pass_data");
    cb_material_upload = std::make_unique<upload_buffer>(device, 1, sizeof(material_data), true, "material_data");
    cb_transforms_upload = std::make_unique<upload_buffer>(device, 1, sizeof(model_data), true, "model_data");
}

frame_resource::~frame_resource()
{
}
