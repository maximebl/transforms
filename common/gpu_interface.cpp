#include "gpu_interface.h"
#include <d3dcompiler.h>
#include "PathCch.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

device_resources::device_resources() : last_signaled_fence_value(0)
{

#ifdef DX12_ENABLE_DEBUG_LAYER
    if (SUCCEEDED(D3D12GetDebugInterface(__uuidof(ID3D12Debug1), (void **)&debug)))
    {
        debug->EnableDebugLayer();
        debug->SetEnableGPUBasedValidation(true);
    }
    DXGIGetDebugInterface1(0, IID_PPV_ARGS(&graphics_analysis));
#endif

    check_hr(CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, IID_PPV_ARGS(&dxgi_factory)));

    check_hr(dxgi_factory->EnumAdapterByGpuPreference(0,
                                                      DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                      IID_PPV_ARGS(&adapter)));

    check_hr(D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(&device)));
    NAME_D3D12_OBJECT(device);

    srv_desc_handle_incr_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // create command objects
    {
        D3D12_COMMAND_QUEUE_DESC cmd_queue_desc;
        cmd_queue_desc.NodeMask = DEFAULT_NODE;
        cmd_queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_HIGH;
        cmd_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        cmd_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        check_hr(device->CreateCommandQueue(&cmd_queue_desc, IID_PPV_ARGS(&cmd_queue)));
        NAME_D3D12_OBJECT(cmd_queue);
    }

    // create debug objects
    {
        check_hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&readback_alloc)));
        NAME_D3D12_OBJECT(readback_alloc);
        check_hr(device->CreateCommandList(DEFAULT_NODE, D3D12_COMMAND_LIST_TYPE_COPY, readback_alloc, nullptr, IID_PPV_ARGS(&readback_list)));
        NAME_D3D12_OBJECT(readback_list);

        check_hr(device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_READBACK),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Buffer(D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT),
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&debug_readback_resource)));
        NAME_D3D12_OBJECT(debug_readback_resource);

        D3D12_COMMAND_QUEUE_DESC cmd_queue_desc = {};
        cmd_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        cmd_queue_desc.NodeMask = DEFAULT_NODE;
        cmd_queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        cmd_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
        check_hr(device->CreateCommandQueue(&cmd_queue_desc, IID_PPV_ARGS(&debug_copy_queue)));
        NAME_D3D12_OBJECT(debug_copy_queue);

        check_hr(device->CreateFence(copy_fence_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copy_fence)));
        copy_fence_event = CreateEventEx(nullptr, NULL, NULL, EVENT_ALL_ACCESS);
    }

    check_hr(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    NAME_D3D12_OBJECT(fence);

    DXGI_SWAP_CHAIN_DESC1 sd;
    {
        ZeroMemory(&sd, sizeof(sd));
        sd.BufferCount = NUM_BACK_BUFFERS;
        sd.Width = 0;
        sd.Height = 0;
        sd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Flags = swapchain_flags;
        sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sd.SampleDesc.Count = 1;
        sd.SampleDesc.Quality = 0;
        sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Scaling = DXGI_SCALING_STRETCH;
        sd.Stereo = FALSE;
    }

    IDXGISwapChain1 *swap_chain = NULL;
    check_hr(dxgi_factory->CreateSwapChainForHwnd(
        (IUnknown *)cmd_queue,
        g_hwnd,
        &sd,
        NULL,
        NULL,
        &swap_chain));

    check_hr(swap_chain->QueryInterface(IID_PPV_ARGS(&swapchain)));
    safe_release(swap_chain);

    check_hr(swapchain->SetMaximumFrameLatency(NUM_BACK_BUFFERS));

    swapchain_event = swapchain->GetFrameLatencyWaitableObject();
    cpu_wait_event = CreateEventEx(nullptr, NULL, NULL, EVENT_ALL_ACCESS);

    {
        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc;
        rtv_heap_desc.NodeMask = DEFAULT_NODE;
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.NumDescriptors = NUM_BACK_BUFFERS;
        rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        check_hr(device->CreateDescriptorHeap(&rtv_heap_desc, IID_PPV_ARGS(&rtv_desc_heap)));
        NAME_D3D12_OBJECT(rtv_desc_heap);

        rtv_handle_incr_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_desc_heap->GetCPUDescriptorHandleForHeapStart();

        for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        {
            rtv_descriptor_handles[i] = rtv_handle;
            rtv_handle.ptr += rtv_handle_incr_size;
        }
    }

    // create render targets (RTVs)
    create_rendertargets();

    {
        D3D12_DESCRIPTOR_HEAP_DESC dsv_heap_desc;
        dsv_heap_desc.NodeMask = DEFAULT_NODE;
        dsv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        dsv_heap_desc.NumDescriptors = 1;
        dsv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        check_hr(device->CreateDescriptorHeap(&dsv_heap_desc, IID_PPV_ARGS(&dsv_heap)));
        NAME_D3D12_OBJECT(dsv_heap);
    }

    // create depth-stencil view (DSV)
    create_dsv(g_hwnd_width, g_hwnd_height);
}

void device_resources::create_rootsig(std::vector<CD3DX12_ROOT_PARAMETER1> *params,
                                      std::vector<CD3DX12_STATIC_SAMPLER_DESC> *samplers)
{
    ID3DBlob *rs_blob = NULL;

    D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data = {};
    feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    check_hr(device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, (void *)&feature_data, sizeof(feature_data)));

    ID3DBlob *error_blob = NULL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_rootsig_desc = {};
    versioned_rootsig_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned_rootsig_desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ASSERT2(params, "At least 1 root signature parameter is required");
    versioned_rootsig_desc.Desc_1_1.NumParameters = (UINT)params->size();
    versioned_rootsig_desc.Desc_1_1.pParameters = params->data();

    if (samplers)
    {
        versioned_rootsig_desc.Desc_1_1.NumStaticSamplers = (UINT)samplers->size();
        versioned_rootsig_desc.Desc_1_1.pStaticSamplers = samplers->data();
    }

    hr = D3D12SerializeVersionedRootSignature(&versioned_rootsig_desc, &rs_blob, &error_blob);

    if (error_blob)
    {
        char *error_msg = (char *)error_blob->GetBufferPointer();
        OutputDebugStringA(error_msg);
    }
    safe_release(error_blob);
    check_hr(hr);

    check_hr(device->CreateRootSignature(
        DEFAULT_NODE,
        rs_blob->GetBufferPointer(),
        rs_blob->GetBufferSize(),
        IID_PPV_ARGS(&rootsig)));

    NAME_D3D12_OBJECT(rootsig);
}

void device_resources::create_rendertargets()
{
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        ID3D12Resource *back_buffer = NULL;
        check_hr(swapchain->GetBuffer(i, IID_PPV_ARGS(&back_buffer)));
        device->CreateRenderTargetView(back_buffer, NULL, rtv_descriptor_handles[i]);
        back_buffers[i] = back_buffer;

        NAME_D3D12_OBJECT_INDEXED(back_buffer, i);
    }
}

void device_resources::create_dsv(UINT64 width, UINT height)
{
    D3D12_CLEAR_VALUE clear_value;
    clear_value.DepthStencil.Depth = 1.0f;
    clear_value.DepthStencil.Stencil = 0;
    clear_value.Format = dsv_format;

    check_hr(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(
            dsv_format,
            g_hwnd_width,
            g_hwnd_height,
            1, 0, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear_value,
        IID_PPV_ARGS(&dsv_resource)));
    NAME_D3D12_OBJECT(dsv_resource);

    D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    dsv_desc.Flags = D3D12_DSV_FLAG_NONE;
    dsv_desc.Format = dsv_format;
    dsv_desc.Texture2D.MipSlice = 0;
    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    device->CreateDepthStencilView(
        dsv_resource,
        &dsv_desc,
        dsv_heap->GetCPUDescriptorHandleForHeapStart());
}

D3D12_GRAPHICS_PIPELINE_STATE_DESC device_resources::create_default_pso_desc(std::vector<D3D12_INPUT_ELEMENT_DESC> *input_layouts)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.NodeMask = DEFAULT_NODE;
    desc.pRootSignature = rootsig;
    desc.InputLayout = {input_layouts->data(), (UINT)input_layouts->size()};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC1(D3D12_DEFAULT);
    desc.DSVFormat = dsv_format;

    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RTVFormats[0] = rtv_format;
    desc.NumRenderTargets = 1;
    desc.SampleMask = UINT_MAX;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    return desc;
}

void device_resources::resize_swapchain(int width, int height)
{
    swapchain->ResizeBuffers(NUM_BACK_BUFFERS, width, height, rtv_format, swapchain_flags);
}

void device_resources::cleanup_rendertargets()
{
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        safe_release(back_buffers[i]);
}

device_resources::~device_resources()
{
    safe_release(rootsig);
    safe_release(dsv_heap);
    safe_release(dsv_resource);
    safe_release(rtv_desc_heap);

    // cleanup render targets
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        safe_release(back_buffers[i]);

    safe_release(adapter);
    safe_release(dxgi_factory);
    safe_release(swapchain);
    safe_release(cmd_queue);
    safe_release(fence);
    safe_release(device);
    CloseHandle(swapchain_event);
    CloseHandle(cpu_wait_event);
}

void device_resources::cpu_wait(UINT64 fence_value)
{
    if (fence_value == 0)
        return; // No fence was signaled
    if (fence->GetCompletedValue() >= fence_value)
        return; // We're already exactly at that fence value, or past that fence value

    fence->SetEventOnCompletion(fence_value, cpu_wait_event);

    DWORD status = WaitForSingleObject(cpu_wait_event, INFINITE);

    if (status == WAIT_OBJECT_0)
    {
        PIXNotifyWakeFromFenceSignal(cpu_wait_event);
    }
}

void device_resources::cpu_wait_for_present_and_fence(UINT64 fence_value)
{
    HANDLE waitable_objects[] = {swapchain_event, NULL};

    if (fence_value != 0)
    {
        fence->SetEventOnCompletion(fence_value, cpu_wait_event);
        waitable_objects[1] = cpu_wait_event;
    }

    DWORD status = WaitForMultipleObjects(_countof(waitable_objects), waitable_objects, TRUE, INFINITE);
    if (status == WAIT_OBJECT_0)
    {
        PIXNotifyWakeFromFenceSignal(cpu_wait_event);
    }
}

void device_resources::flush_cmd_queue()
{
    UINT64 fence_value = ++last_signaled_fence_value;
    cmd_queue->Signal(fence, fence_value);
    cpu_wait(fence_value);
}

void device_resources::wait_present()
{
    WaitForSingleObject(swapchain_event, INFINITE);
}

void device_resources::wait_last_frame()
{
    cpu_wait(last_signaled_fence_value);
}

UINT64 device_resources::signal()
{
    cmd_queue->Signal(fence, ++last_signaled_fence_value);
    return last_signaled_fence_value;
}

void device_resources::resize(int width, int height)
{
    g_hwnd_width = width;
    g_hwnd_height = height;

    cleanup_rendertargets();
    safe_release(dsv_resource);

    resize_swapchain(width, height);
    create_rendertargets();
    create_dsv(width, height);
}

void device_resources::begin_capture()
{
    if (graphics_analysis != nullptr)
        graphics_analysis->BeginCapture();
}

void device_resources::end_capture()
{
    if (graphics_analysis != nullptr)
        graphics_analysis->EndCapture();
}

void device_resources::present(bool is_vsync)
{
    UINT sync_interval = is_vsync ? 1 : 0;
    UINT present_flags = is_vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING;
    swapchain->Present(sync_interval, present_flags);
}

std::vector<mesh_data> meshes;

mesh_data process_mesh(aiMesh *mesh, const aiScene *scene)
{
    mesh_data meshdata;
    std::vector<position_color> vertices;
    std::vector<WORD> indices;

    for (unsigned int i = 0; i < mesh->mNumVertices; i++)
    {
        position_color vertex;
        vertex.position.x = mesh->mVertices[i].x;
        vertex.position.y = mesh->mVertices[i].y;
        vertex.position.z = mesh->mVertices[i].z;
        vertex.color = {1.f, 1.f, 1.f, 1.f};
        vertices.push_back(vertex);
    }
    // process indices
    for (unsigned int i = 0; i < mesh->mNumFaces; i++)
    {
        aiFace face = mesh->mFaces[i];
        for (unsigned int j = 0; j < face.mNumIndices; j++)
            indices.push_back(face.mIndices[j]);
    }

    meshdata.vertices = vertices;
    meshdata.indices = indices;
    meshdata.name = mesh->mName.C_Str();
    return meshdata;
}

void process_node(aiNode *node, const aiScene *scene)
{
    // process all the node's meshes (if any)
    for (unsigned int i = 0; i < node->mNumMeshes; i++)
    {
        aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
        meshes.push_back(process_mesh(mesh, scene));
    }
    // then do the same for each of its children
    for (unsigned int i = 0; i < node->mNumChildren; i++)
    {
        process_node(node->mChildren[i], scene);
    }
}

COMMON_API std::vector<mesh_data> import_meshdata(const char *path)
{
    Assimp::Importer importer;
    importer.SetPropertyFloat(AI_CONFIG_PP_GSN_MAX_SMOOTHING_ANGLE, 80.0f);
    importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
    unsigned int preprocess_flags = aiProcessPreset_TargetRealtime_MaxQuality | aiProcess_OptimizeGraph;

    const aiScene *scene = importer.ReadFile(path, preprocess_flags);
    OutputDebugStringA(importer.GetErrorString());

    meshes.clear();
    process_node(scene->mRootNode, scene);
    return meshes;
}

void set_viewport_rects(ID3D12GraphicsCommandList *cmd_list)
{
    D3D12_RECT rect;
    rect.left = 0;
    rect.top = 0;
    rect.right = (LONG)g_hwnd_width;
    rect.bottom = (LONG)g_hwnd_height;
    cmd_list->RSSetScissorRects(1, &rect);

    D3D12_VIEWPORT vp;
    vp.TopLeftY = 0;
    vp.TopLeftX = 0;
    vp.Width = (float)g_hwnd_width;
    vp.Height = (float)g_hwnd_height;
    vp.MaxDepth = 1.0f;
    vp.MinDepth = 0.0f;
    cmd_list->RSSetViewports(1, &vp);
}

COMMON_API D3D12_DEPTH_STENCIL_DESC create_outline_dss()
{
    D3D12_DEPTH_STENCIL_DESC outline_dss = {};
    outline_dss.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    outline_dss.DepthEnable = true;
    outline_dss.StencilEnable = true;
    outline_dss.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    outline_dss.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    outline_dss.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    outline_dss.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_NOT_EQUAL;
    outline_dss.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    outline_dss.FrontFace.StencilPassOp = D3D12_STENCIL_OP_KEEP;
    outline_dss.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    outline_dss.BackFace = outline_dss.FrontFace;
    return outline_dss;
}

COMMON_API D3D12_DEPTH_STENCIL_DESC create_stencil_dss()
{
    D3D12_DEPTH_STENCIL_DESC stencil_dss = {};
    stencil_dss.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO; // dont write to the depth buffer
    stencil_dss.StencilEnable = true;
    stencil_dss.DepthEnable = true;
    stencil_dss.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    stencil_dss.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
    stencil_dss.DepthFunc = D3D12_COMPARISON_FUNC_LESS;

    stencil_dss.FrontFace.StencilFunc = D3D12_COMPARISON_FUNC_ALWAYS; // always write to the stencil buffer
    stencil_dss.FrontFace.StencilPassOp = D3D12_STENCIL_OP_REPLACE;
    stencil_dss.FrontFace.StencilFailOp = D3D12_STENCIL_OP_KEEP;
    stencil_dss.FrontFace.StencilDepthFailOp = D3D12_STENCIL_OP_KEEP;
    stencil_dss.BackFace = stencil_dss.FrontFace;
    return stencil_dss;
}

bool compile_shader(const wchar_t *file, const wchar_t *entry, shader_type type, ID3DBlob **blob)
{
    WIN32_FIND_DATAW found_file;
    if (FindFirstFileW(file, &found_file) == INVALID_HANDLE_VALUE)
    {
        wchar_t can_file[MAX_PATH];
        PathCchCanonicalize(can_file, MAX_PATH, file);

        wchar_t cur_dir[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, cur_dir);

        wchar_t err_msg[MAX_PATH];
        wcscpy(err_msg, L"Shader file not found:");
        wcscat(err_msg, L"\n");
        wcscat(err_msg, can_file);
        wcscat(err_msg, L"\n\n");

        wcscat(err_msg, L"Working directory:");
        wcscat(err_msg, L"\n");
        wcscat(err_msg, cur_dir);
        wcscat(err_msg, L"\n\n");

        wcscat(err_msg, L"File input:");
        wcscat(err_msg, L"\n");
        wcscat(err_msg, file);

        if (MessageBoxW(NULL, err_msg,
                        L"Could not find shader.",
                        MB_OK | MB_ICONERROR | MB_DEFBUTTON2) != IDYES)
        {
            return false;
        }
    }

    ID3DBlob *error_blob = NULL;

    char shader_target_str[10];
    switch (type)
    {
    case vertex:
        strcpy(shader_target_str, "vs_5_1");
        break;
    case domain:
        strcpy(shader_target_str, "ds_5_1");
        break;
    case hull:
        strcpy(shader_target_str, "hs_5_1");
        break;
    case geometry:
        strcpy(shader_target_str, "gs_5_1");
        break;
    case pixel:
        strcpy(shader_target_str, "ps_5_1");
        break;
    case compute:
        strcpy(shader_target_str, "cs_5_1");
    default:
        break;
    }

    char entry_str[20];
    wcstombs(entry_str, entry, 20);
    hr = D3DCompileFromFile(file,
                            NULL,
                            D3D_COMPILE_STANDARD_FILE_INCLUDE,
                            entry_str,
                            shader_target_str,
                            D3DCOMPILE_SKIP_OPTIMIZATION | D3DCOMPILE_DEBUG,
                            0,
                            blob,
                            &error_blob);
    if (error_blob)
    {
        char *error_msg = (char *)error_blob->GetBufferPointer();
        OutputDebugStringA(error_msg);

        if (MessageBoxA(NULL, error_msg,
                        "Shader compilation error.",
                        MB_OK | MB_ICONERROR | MB_DEFBUTTON2) != IDYES)
        {
            return false;
        }
    }
    check_hr(hr);

    return true;
}

void create_default_buffer(ID3D12Device *device,
                           ID3D12GraphicsCommandList *cmd_list,
                           const void *data,
                           size_t byte_size,
                           ID3D12Resource **upload_resource,
                           ID3D12Resource **default_resource,
                           const char *name,
                           D3D12_RESOURCE_FLAGS flags)
{

    check_hr(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byte_size, flags),
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(default_resource)));
    ID3D12Resource *p_default_resource = (*default_resource);

    char resource_name[50];

    strcpy(resource_name, name);
    strcat(resource_name, "_default_resource");
    set_name(p_default_resource, resource_name);

    check_hr(device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byte_size),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(upload_resource)));
    ID3D12Resource *p_upload_resource = (*upload_resource);

    strcpy(resource_name, name);
    strcat(resource_name, "_upload_resource");
    set_name(p_upload_resource, resource_name);

    BYTE *mapped_data = nullptr;
    D3D12_RANGE range = {};
    p_upload_resource->Map(0, &range, (void **)&mapped_data);
    if (data)
        memcpy((void *)mapped_data, data, byte_size);

    p_upload_resource->Unmap(0, &range);

    cmd_list->CopyBufferRegion(
        p_default_resource, 0,
        p_upload_resource, 0,
        byte_size);
}

void create_mesh_data(ID3D12Device *device, ID3D12GraphicsCommandList *cmd_list, const char *name,
                      size_t vertex_stride, size_t vertex_count, void *vertex_data,
                      size_t index_stride, size_t index_count, void *index_data,
                      mesh *mesh)
{
    mesh->resource = new mesh_resource;
    mesh->name = name;

    // vertex data
    mesh->vertex_count = (UINT)vertex_count;
    size_t byte_size = vertex_stride * vertex_count;
    mesh->resource->vbv.StrideInBytes = (UINT)vertex_stride;
    mesh->resource->vbv.SizeInBytes = (UINT)byte_size;

    char resource_name[50];
    strcpy(resource_name, name);
    strcat(resource_name, "_vertex");
    create_default_buffer(device, cmd_list,
                          vertex_data, byte_size,
                          &mesh->resource->vertex_upload, &mesh->resource->vertex_default, resource_name);

    mesh->resource->vbv.BufferLocation = mesh->resource->vertex_default->GetGPUVirtualAddress();

    if (index_count > 0)
    {
        // index data
        mesh->index_count = (UINT)index_count;
        byte_size = index_stride * index_count;
        mesh->resource->ibv.Format = DXGI_FORMAT_R16_UINT;
        mesh->resource->ibv.SizeInBytes = (UINT)byte_size;

        strcpy(resource_name, name);
        strcat(resource_name, "_index");
        create_default_buffer(device, cmd_list,
                              index_data, byte_size,
                              &mesh->resource->index_upload, &mesh->resource->index_default, resource_name);

        mesh->resource->ibv.BufferLocation = mesh->resource->index_default->GetGPUVirtualAddress();

        cmd_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                         mesh->resource->index_default,
                                         D3D12_RESOURCE_STATE_COPY_DEST,
                                         D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
    }

    cmd_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                     mesh->resource->vertex_default,
                                     D3D12_RESOURCE_STATE_COPY_DEST,
                                     D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER));
}

upload_buffer::upload_buffer(ID3D12Device *device, size_t max_element_count, size_t element_byte_size, bool is_constant_buffer, const char *name)
    : m_element_byte_size(element_byte_size), m_max_element_count((UINT)max_element_count)
{
    m_buffer_size = m_element_byte_size * max_element_count;

    if (is_constant_buffer)
    {
        m_buffer_size = align_up(m_buffer_size, D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
    }

    device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(m_buffer_size),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&m_uploadbuffer));

    wchar_t wname[50];
    mbstowcs(wname, name, 50);
    wcscat(wname, L"_upload_resource");
    m_uploadbuffer->SetName(wname);

    m_uploadbuffer->Map(0, nullptr, reinterpret_cast<void **>(&m_mapped_data));
}

upload_buffer::~upload_buffer()
{
    if (m_uploadbuffer != nullptr)
        m_uploadbuffer->Unmap(0, nullptr);

    m_mapped_data = nullptr;
    safe_release(m_uploadbuffer);
}

void upload_buffer::clear_data()
{
    ZeroMemory(m_mapped_data, m_buffer_size);
}

void upload_buffer::copy_data(int elementIndex, const void *data)
{
    memcpy(&m_mapped_data[elementIndex * m_element_byte_size], data, m_element_byte_size);
}

//void device_resources::draw_debug_lines(ID3D12GraphicsCommandList *cmd_list, std::vector<debug_line *> *debug_lines)
//{
//    if (!is_line_buffer_ready)
//    {
//        compile_shader(L"..\\..\\3d_transforms\\shaders\\debug_fx.hlsl", L"VS", shader_type::vertex, &debugfx_blob_vs);
//        compile_shader(L"..\\..\\3d_transforms\\shaders\\debug_fx.hlsl", L"PS", shader_type::pixel, &debugfx_blob_ps);
//
//        std::vector<D3D12_INPUT_ELEMENT_DESC> input_elem_descs;
//        input_elem_descs.push_back({"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
//        input_elem_descs.push_back({"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
//
//        D3D12_GRAPHICS_PIPELINE_STATE_DESC default_pso_desc = create_default_pso_desc(&input_elem_descs, debugfx_blob_vs, debugfx_blob_ps);
//        default_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
//        default_pso_desc.VS = {debugfx_blob_vs->GetBufferPointer(), debugfx_blob_vs->GetBufferSize()};
//        hr = device->CreateGraphicsPipelineState(&default_pso_desc, IID_PPV_ARGS(&line_pso));
//        ASSERT(SUCCEEDED(hr));
//
//        debug_lines->push_back(&orbit_line);
//        size_t line_stride = sizeof(position_color);
//        size_t lines_byte_size = line_stride * max_debug_lines;
//        debug_lines_upload = new upload_buffer(device, 1, lines_byte_size, "debug_lines");
//
//        debug_lines_vbv = {};
//        debug_lines_vbv.BufferLocation = debug_lines_upload->m_uploadbuffer->GetGPUVirtualAddress();
//        debug_lines_vbv.SizeInBytes = (UINT)lines_byte_size;
//        debug_lines_vbv.StrideInBytes = (UINT)line_stride;
//        is_line_buffer_ready = true;
//    }
//
//    if (debug_lines->size() > 0)
//    {
//        debug_lines_upload->copy_data(0, (void *)*debug_lines->data());
//
//        cmd_list->SetPipelineState(line_pso);
//        cmd_list->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
//        cmd_list->IASetVertexBuffers(0, 1, &debug_lines_vbv);
//        cmd_list->DrawInstanced(2, 1, 0, 0);
//    }
//}
