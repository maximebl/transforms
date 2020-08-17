#include "pch.h"
#include "frame_resource.h"

frame_cmd::~frame_cmd()
{
    safe_release(cmd_alloc);
}

frame_resource::frame_resource(ID3D12Device *device, size_t frame_index, BYTE *particle_data)
{
    hr = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&cmd_alloc));
    ASSERT(SUCCEEDED(hr));
    NAME_D3D12_OBJECT_INDEXED(cmd_alloc, (UINT)frame_index);

    particle_vb_range = particle_data;

    size_t cb_passdata_size = align_up(sizeof(pass_data), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    cb_passdata_upload = std::make_unique<upload_buffer>(device, 1, cb_passdata_size, "pass_data");
}

frame_resource::~frame_resource()
{
}
