#pragma once
#include "pch.h"
#include "../imgui/imgui.h" //it's necessary to include imgui.h before imgui_impl_dx12 and imgui_impl_win32
#include "../imgui/imgui_impl_dx12.h"
#include "../imgui/imgui_impl_win32.h"
#include "../common/gpu_interface.h"
#include "../common/gpu_query.h"

extern "C" __declspec(dllexport) bool update_and_render();
extern "C" __declspec(dllexport) void resize(int width, int height);
extern "C" __declspec(dllexport) void cleanup();
extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) bool initialize();

internal device_resources *dr;
internal ID3D12GraphicsCommandList *cmd_list;
internal ID3D12CommandAllocator *cmd_alloc;
internal gpu_query *queries;
internal ID3DBlob *outline_blob;
internal bool is_vsync;

extern "C" __declspec(dllexport) bool initialize()
{
    if (!DirectX::XMVerifyCPUSupport())
        return false;

    dr = new device_resources();

    //LARGE_INTEGER tmp_cpu_frequency;
    //QueryPerformanceFrequency(&tmp_cpu_frequency);
    //g_cpu_frequency = (double)tmp_cpu_frequency.QuadPart;

    if (!compile_shader(L"..\\..\\transforms\\outline.hlsl", &outline_blob))
        return false;

    hr = dr->device->CreateCommandList(DEFAULT_NODE,
                                       D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       dr->frame_cmds->cmd_alloc,
                                       nullptr,
                                       __uuidof(ID3D12GraphicsCommandList),
                                       (void **)&cmd_list);
    ASSERT(SUCCEEDED(hr));
    cmd_list->Close();

    return true;
}

extern "C" __declspec(dllexport) bool update_and_render()
{
    dr->wait_last_present();

    UINT backbuffer_index = dr->swapchain->GetCurrentBackBufferIndex(); // gets updated after each call to Present()
    dr->backbuffer_index = backbuffer_index;

    frame_cmd frame = dr->frame_cmds[backbuffer_index];
    cmd_alloc = frame.cmd_alloc;

    dr->wait_last_frame();

    cmd_alloc->Reset();
    cmd_list->Reset(cmd_alloc, nullptr);

    cmd_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                     dr->main_rt_resources[backbuffer_index],
                                     D3D12_RESOURCE_STATE_PRESENT,
                                     D3D12_RESOURCE_STATE_RENDER_TARGET));

    set_viewport_rects(cmd_list);
    cmd_list->ClearDepthStencilView(dr->dsv_heap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.f, 0, 0, NULL);
    cmd_list->ClearRenderTargetView(dr->rtv_descriptors[backbuffer_index], DirectX::Colors::IndianRed, 0, NULL);
    cmd_list->OMSetRenderTargets(1, &dr->rtv_descriptors[backbuffer_index], FALSE, &dr->dsv_heap->GetCPUDescriptorHandleForHeapStart());

    cmd_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                     dr->main_rt_resources[backbuffer_index],
                                     D3D12_RESOURCE_STATE_RENDER_TARGET,
                                     D3D12_RESOURCE_STATE_PRESENT));

    cmd_list->Close();
    dr->cmd_queue->ExecuteCommandLists((UINT)1, (ID3D12CommandList *const *)&cmd_list);

    dr->present(is_vsync);
    dr->signal();
    return true;
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam)
{
    ImGui_ImplWin32_WndProcHandler(g_hwnd, msg, wParam, lParam);
}

extern "C" __declspec(dllexport) void resize(int width, int height)
{
}

extern "C" __declspec(dllexport) void cleanup()
{
    dr->flush_cmd_queue();
    safe_release(cmd_list);
    delete dr;

#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1 *debug = NULL;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, __uuidof(IDXGIDebug1), (void **)&debug)))
    {
        debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
        debug->Release();
    }
#endif
}
