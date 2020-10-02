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

frame_resource::frame_resource(ID3D12Device *device, size_t frame_index, size_t element_count, size_t instance_count)
    : frame_cmd(device, frame_index)
{
    cb_objconstant_upload = new upload_buffer(device, element_count, sizeof(object_data), true, "obj_constants");
    cb_pass_upload = new upload_buffer(device, 1, sizeof(pass_data), true, "pass_data");

    sb_instancedata_upload = new upload_buffer(device, instance_count, sizeof(instance_data), false, "instance_data");
    sb_instanceIDs_upload = new upload_buffer(device, instance_count, sizeof(UINT), false, "instance_IDs");
    sb_selected_instanceIDs_upload = new upload_buffer(device, instance_count, sizeof(UINT), false, "selected_instance_IDs");
}

frame_resource::~frame_resource()
{
    delete cb_objconstant_upload;
    delete sb_instancedata_upload;
    delete cb_pass_upload;
    delete sb_instanceIDs_upload;
    delete sb_selected_instanceIDs_upload;
}
