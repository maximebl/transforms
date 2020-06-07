#include "frame_resource.h"

frame_cmd::frame_cmd(ID3D12Device *device, size_t frame_index) : m_frame_index(frame_index)
{
    hr = device->CreateCommandAllocator(
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        IID_PPV_ARGS(&cmd_alloc));
    ASSERT(SUCCEEDED(hr));

    wchar_t buf[50];
    swprintf_s(buf, 50, L"%s%zu", L"frame_cmd_alloc_", frame_index);
    cmd_alloc->SetName(buf);
}

frame_cmd::~frame_cmd()
{
    safe_release(cmd_alloc);
}

frame_resource::frame_resource(ID3D12Device *device, size_t frame_index, UINT element_count, UINT instance_count, UINT pass_count)
    : frame_cmd(device, frame_index)
{
    cb_objconstants_size = (UINT)align_up(sizeof(object_data), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    cb_objconstant_upload = new upload_buffer(device, element_count, cb_objconstants_size, "obj_constants");

    sb_instancedata_upload = new upload_buffer(device, instance_count, sizeof(instance_data), "instance_data");

    UINT cb_passdata_size = (UINT)align_up(sizeof(pass_data), D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    cb_passdata_upload = new upload_buffer(device, pass_count, cb_passdata_size, "pass_data");
}

frame_resource::~frame_resource()
{
    delete cb_objconstant_upload;
    delete sb_instancedata_upload;
    delete cb_passdata_upload;
}