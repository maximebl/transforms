#include "gpu_interface.h"
#include <d3dcompiler.h>
#include "PathCch.h"
#include "../imgui/imgui.h" //it's necessary to include imgui.h before imgui_impl_dx12 and imgui_impl_win32
#include "../imgui/imgui_impl_dx12.h"
#include "../imgui/imgui_impl_win32.h"

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

    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        hr = device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE::D3D12_COMMAND_LIST_TYPE_DIRECT,
            __uuidof(ID3D12CommandAllocator),
            (void **)&frame_cmds[i].cmd_alloc);
        ASSERT(SUCCEEDED(hr));

        wchar_t buf[50];
        swprintf_s(buf, 50, L"%s%d", L"main_cmd_alloc_", i);
        frame_cmds[i].cmd_alloc->SetName(buf);
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
        sd.Format = DXGI_FORMAT::DXGI_FORMAT_R8G8B8A8_UNORM;
        sd.Flags = DXGI_SWAP_CHAIN_FLAG::DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT | DXGI_SWAP_CHAIN_FLAG::DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;
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

    {
        D3D12_DESCRIPTOR_HEAP_DESC imgui_heap_desc;
        imgui_heap_desc.NodeMask = DEFAULT_NODE;
        imgui_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        imgui_heap_desc.NumDescriptors = 1;
        imgui_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = device->CreateDescriptorHeap(&imgui_heap_desc, __uuidof(ID3D12DescriptorHeap), (void **)&imgui_srv_heap);

        ASSERT(SUCCEEDED(hr));
        imgui_srv_heap->SetName(L"imgui_srv_heap");
    }

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

    // create imgui objects
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hwnd);

    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_handle = imgui_srv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu_handle = imgui_srv_heap->GetGPUDescriptorHandleForHeapStart();

    ImGui_ImplDX12_Init(device,
                        NUM_BACK_BUFFERS,
                        DXGI_FORMAT_R8G8B8A8_UNORM,
                        imgui_srv_heap, //unused for now
                        srv_cpu_handle,
                        srv_gpu_handle);
}

device_resources::~device_resources()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    safe_release(dsv_heap);
    safe_release(dsv_resource);
    safe_release(imgui_srv_heap);
    safe_release(rtv_desc_heap);

    // cleanup render targets
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        if (main_rt_resources[i])
        {
            safe_release(main_rt_resources[i]);
        }
    }

    for (int i = 0; i < NUM_BACK_BUFFERS; ++i)
        safe_release(frame_cmds[i].cmd_alloc);

    safe_release(adapter);
    safe_release(dxgi_factory);
    safe_release(swapchain);
    safe_release(debug);
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

void device_resources::wait_last_present()
{
    WaitForSingleObject(swapchain_event, INFINITE);
}

void device_resources::wait_last_frame()
{
    cpu_wait(last_signaled_fence_value);
}

void device_resources::wait_current_frame()
{
    cpu_wait(frame_cmds[backbuffer_index].fence_value);
}

void device_resources::signal()
{
    cmd_queue->Signal(fence, ++last_signaled_fence_value);
    frame_cmds[backbuffer_index].fence_value = last_signaled_fence_value;
}

void device_resources::present(bool is_vsync)
{
    UINT sync_interval = is_vsync ? 1 : 0;
    UINT present_flags = is_vsync ? 0 : DXGI_PRESENT_ALLOW_TEARING;
    swapchain->Present(sync_interval, present_flags);
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

bool compile_shader(const wchar_t *file, ID3DBlob **blob)
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

    hr = D3DCompileFromFile(file,
                            NULL,
                            D3D_COMPILE_STANDARD_FILE_INCLUDE,
                            "VS",
                            "vs_5_1",
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
