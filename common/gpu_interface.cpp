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
#endif

    hr = CreateDXGIFactory2(DXGI_CREATE_FACTORY_DEBUG, __uuidof(IDXGIFactory6), (void **)&dxgi_factory);
    ASSERT(SUCCEEDED(hr));

    hr = dxgi_factory->EnumAdapterByGpuPreference(0,
                                                  DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
                                                  __uuidof(IDXGIAdapter4), (void **)&adapter);
    ASSERT(SUCCEEDED(hr));

    hr = D3D12CreateDevice((IUnknown *)adapter, D3D_FEATURE_LEVEL_12_1, __uuidof(ID3D12Device), (void **)&device);
    ASSERT(SUCCEEDED(hr));

    device->SetName(L"main_device");

    srv_desc_handle_incr_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // create command objects
    {
        D3D12_COMMAND_QUEUE_DESC cmd_queue_desc;
        cmd_queue_desc.NodeMask = DEFAULT_NODE;
        cmd_queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY::D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        cmd_queue_desc.Type = D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT;
        cmd_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAGS::D3D12_COMMAND_QUEUE_FLAG_NONE;
        hr = device->CreateCommandQueue(&cmd_queue_desc, __uuidof(ID3D12CommandQueue), (void **)&cmd_queue);

        ASSERT(SUCCEEDED(hr));
        cmd_queue->SetName(L"main_cmd_queue");
    }

    hr = device->CreateFence(0, D3D12_FENCE_FLAGS::D3D12_FENCE_FLAG_NONE, __uuidof(ID3D12Fence), (void **)&fence);
    ASSERT(SUCCEEDED(hr));
    fence->SetName(L"main_fence");

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
        sd.SwapEffect = DXGI_SWAP_EFFECT::DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sd.AlphaMode = DXGI_ALPHA_MODE::DXGI_ALPHA_MODE_UNSPECIFIED;
        sd.Scaling = DXGI_SCALING::DXGI_SCALING_STRETCH;
        sd.Stereo = FALSE;
    }

    IDXGISwapChain1 *swap_chain = NULL;
    hr = dxgi_factory->CreateSwapChainForHwnd(
        (IUnknown *)cmd_queue,
        g_hwnd,
        &sd,
        NULL,
        NULL,
        &swap_chain);
    ASSERT(SUCCEEDED(hr));

    hr = swap_chain->QueryInterface(__uuidof(IDXGISwapChain1), (void **)&(swapchain));
    ASSERT(SUCCEEDED(hr));
    swap_chain->Release();

    hr = swapchain->SetMaximumFrameLatency(NUM_BACK_BUFFERS);
    ASSERT(SUCCEEDED(hr));

    swapchain_event = swapchain->GetFrameLatencyWaitableObject();

    {
        D3D12_DESCRIPTOR_HEAP_DESC rtv_heap_desc;
        rtv_heap_desc.NodeMask = DEFAULT_NODE;
        rtv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtv_heap_desc.NumDescriptors = NUM_BACK_BUFFERS;
        rtv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

        hr = device->CreateDescriptorHeap(&rtv_heap_desc, __uuidof(ID3D12DescriptorHeap), (void **)&rtv_desc_heap);
        ASSERT(SUCCEEDED(hr));

        rtv_desc_heap->SetName(L"main_rtv_desc_heap");
        rtv_handle_incr_size = device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = rtv_desc_heap->GetCPUDescriptorHandleForHeapStart();

        for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        {
            rtv_descriptors[i] = rtv_handle;
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
        hr = device->CreateDescriptorHeap(&dsv_heap_desc, __uuidof(ID3D12DescriptorHeap), (void **)&dsv_heap);

        ASSERT(SUCCEEDED(hr));
        dsv_heap->SetName(L"main_dsv_heap");
    }

    // create depth-stencil view (DSV)
    {
        D3D12_CLEAR_VALUE clear_value;
        clear_value.DepthStencil.Depth = 1.0f;
        clear_value.DepthStencil.Stencil = 0;
        clear_value.Format = dsv_format;

        hr = device->CreateCommittedResource(
            &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
            D3D12_HEAP_FLAG_NONE,
            &CD3DX12_RESOURCE_DESC::Tex2D(
                dsv_format,
                g_hwnd_width,
                g_hwnd_height,
                1, 0, 1, 0,
                D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
            D3D12_RESOURCE_STATE_DEPTH_WRITE, &clear_value,
            __uuidof(ID3D12Resource), (void **)&dsv_resource);
        dsv_resource->SetName(L"dsv_resource");
        ASSERT(SUCCEEDED(hr));

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
}

void device_resources::create_rootsig(std::vector<CD3DX12_ROOT_PARAMETER1> *params, const wchar_t *name)
{
    ID3DBlob *rs_blob = NULL;

    D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data = {};
    feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
    hr = device->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, (void *)&feature_data, sizeof(feature_data));
    ASSERT(SUCCEEDED(hr));

    ID3DBlob *error_blob = NULL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC versioned_rootsig_desc;
    versioned_rootsig_desc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    versioned_rootsig_desc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    versioned_rootsig_desc.Desc_1_1.NumParameters = (UINT)params->size();
    versioned_rootsig_desc.Desc_1_1.pParameters = params->data();
    versioned_rootsig_desc.Desc_1_1.NumStaticSamplers = 0;
    versioned_rootsig_desc.Desc_1_1.pStaticSamplers = NULL;
    hr = D3D12SerializeVersionedRootSignature(&versioned_rootsig_desc, &rs_blob, &error_blob);
    ASSERT(SUCCEEDED(hr));

    if (error_blob)
    {
        wchar_t *error_msg = (wchar_t *)error_blob->GetBufferPointer();
        OutputDebugStringW(error_msg);
    }
    safe_release(error_blob);

    hr = device->CreateRootSignature(
        DEFAULT_NODE,
        rs_blob->GetBufferPointer(),
        rs_blob->GetBufferSize(),
        __uuidof(ID3D12RootSignature),
        (void **)&rootsig);
    ASSERT(SUCCEEDED(hr));
    rootsig->SetName(name);
}

void device_resources::create_rendertargets()
{
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        ID3D12Resource *back_buffer = NULL;
        swapchain->GetBuffer(i, __uuidof(ID3D12Resource), (void **)&back_buffer);
        device->CreateRenderTargetView(back_buffer, NULL, rtv_descriptors[i]);
        main_rt_resources[i] = back_buffer;

        wchar_t buf[20];
        swprintf_s(buf, 20, L"%s%d", L"rtv_resource_", i);
        main_rt_resources[i]->SetName(buf);
    }
}

void device_resources::create_dsv(UINT64 width, UINT height)
{
    D3D12_CLEAR_VALUE clear_value;
    clear_value.DepthStencil.Depth = 1.0f;
    clear_value.DepthStencil.Stencil = 0;
    clear_value.Format = dsv_format;

    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Tex2D(
            dsv_format,
            width,
            height,
            1, 0, 1, 0,
            D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL),
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clear_value,
        __uuidof(ID3D12Resource),
        (void **)&dsv_resource);
    ASSERT(SUCCEEDED(hr));

    dsv_resource->SetName(L"dsv_resource");

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

D3D12_GRAPHICS_PIPELINE_STATE_DESC device_resources::create_default_pso(std::vector<D3D12_INPUT_ELEMENT_DESC> *input_layouts, ID3DBlob *vs, ID3DBlob *ps)
{
    D3D12_GRAPHICS_PIPELINE_STATE_DESC desc = {};
    desc.NodeMask = DEFAULT_NODE;
    desc.pRootSignature = rootsig;
    desc.InputLayout = {input_layouts->data(), (UINT)input_layouts->size()};
    desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    desc.VS = {vs->GetBufferPointer(), vs->GetBufferSize()};

    desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC1(D3D12_DEFAULT);
    desc.DSVFormat = dsv_format;

    desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    desc.RTVFormats[0] = rtv_format;
    desc.NumRenderTargets = 1;
    desc.SampleMask = UINT_MAX;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.PS = {ps->GetBufferPointer(), ps->GetBufferSize()};
    return desc;
}

void device_resources::resize_swapchain(int width, int height)
{
    swapchain->ResizeBuffers(NUM_BACK_BUFFERS, width, height, rtv_format, swapchain_flags);
}

void device_resources::cleanup_rendertargets()
{
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        if (main_rt_resources[i])
        {
            main_rt_resources[i]->Release();
            main_rt_resources[i] = nullptr;
        }
}

device_resources::~device_resources()
{
#ifdef DEBUG
    safe_release(debug);
#endif // DEBUG
    safe_release(rootsig);
    safe_release(dsv_heap);
    safe_release(dsv_resource);
    safe_release(rtv_desc_heap);

    // cleanup render targets
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        if (main_rt_resources[i])
        {
            safe_release(main_rt_resources[i]);
        }
    }

    safe_release(adapter);
    safe_release(dxgi_factory);
    safe_release(swapchain);
    safe_release(cmd_queue);
    safe_release(fence);
    safe_release(device);
    CloseHandle(swapchain_event);
}

void device_resources::cpu_wait(UINT64 fence_value)
{
    if (fence_value == 0)
        return; // No fence was signaled
    if (fence->GetCompletedValue() >= fence_value)
        return; // We're already exactly at that fence value, or past that fence value

    HANDLE event = CreateEventEx(nullptr, NULL, NULL, EVENT_ALL_ACCESS);
    fence->SetEventOnCompletion(fence_value, event);

    WaitForSingleObject(event, INFINITE);
    CloseHandle(event);
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
    ASSERT(SUCCEEDED(hr));

    return true;
}

void create_default_buffer(ID3D12Device *device,
                           ID3D12GraphicsCommandList *cmd_list,
                           const void *data,
                           size_t byte_size,
                           ID3D12Resource **upload_resource,
                           ID3D12Resource **default_resource,
                           const wchar_t *name)
{

    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byte_size),
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(default_resource));
    ASSERT(SUCCEEDED(hr));
    ID3D12Resource *p_default_resource = (*default_resource);

    wchar_t resource_name[50];
    wcscpy(resource_name, name);
    wcscat(resource_name, L"_default_resource");
    p_default_resource->SetName(resource_name);

    hr = device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
        D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(byte_size),
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(upload_resource));
    ASSERT(SUCCEEDED(hr));
    ID3D12Resource *p_upload_resource = (*upload_resource);

    wcscpy(resource_name, name);
    wcscat(resource_name, L"_upload_resource");
    p_upload_resource->SetName(resource_name);

    BYTE *mapped_data = nullptr;
    D3D12_RANGE range = {};
    p_upload_resource->Map(0, &range, (void **)&mapped_data);
    memcpy((void *)mapped_data, data, byte_size);

    p_upload_resource->Unmap(0, &range);

    cmd_list->CopyBufferRegion(
        p_default_resource, 0,
        p_upload_resource, 0,
        byte_size);
}

void create_mesh_data(ID3D12Device *device, ID3D12GraphicsCommandList *cmd_list, const wchar_t *name,
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

    wchar_t resource_name[50];
    wcscpy(resource_name, name);
    wcscat(resource_name, L"_vertex");
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

        wcscpy(resource_name, name);
        wcscat(resource_name, L"_index");
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

size_t align_up(size_t value, size_t alignment)
{
    return ((value + (alignment - 1)) & ~(alignment - 1));
}

upload_buffer::upload_buffer(ID3D12Device *device, size_t element_count, size_t element_byte_size, const char *name)
    : m_element_byte_size(element_byte_size)
{
    m_buffer_size = (UINT)m_element_byte_size * (UINT)element_count;

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

void upload_buffer::copy_data(int elementIndex, const void *data)
{
    memcpy(&m_mapped_data[elementIndex * m_element_byte_size], data, m_element_byte_size);
}
