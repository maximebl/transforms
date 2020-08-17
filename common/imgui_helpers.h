#pragma once
#include "common.h"
#include <gpu_interface.h>
#include "../imgui/imgui.h"
#include "../imgui/imgui_impl_dx12.h"
#include "../imgui/imgui_impl_win32.h"

s_internal ID3D12DescriptorHeap *imgui_srv_heap = nullptr;
DirectX::XMFLOAT2 mouse_pos;
DirectX::XMFLOAT2 ndc_mouse_pos;

void imgui_init(ID3D12Device *device)
{
    ImGui::CreateContext();
    ImGui::StyleColorsDark();
    ImGui_ImplWin32_Init(g_hwnd);

    D3D12_DESCRIPTOR_HEAP_DESC imgui_heap_desc;
    imgui_heap_desc.NodeMask = DEFAULT_NODE;
    imgui_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    imgui_heap_desc.NumDescriptors = 1;
    imgui_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(&imgui_heap_desc, __uuidof(ID3D12DescriptorHeap), (void **)&imgui_srv_heap);

    ASSERT(SUCCEEDED(hr));
    imgui_srv_heap->SetName(L"imgui_srv_heap");

    D3D12_CPU_DESCRIPTOR_HANDLE srv_cpu_handle = imgui_srv_heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE srv_gpu_handle = imgui_srv_heap->GetGPUDescriptorHandleForHeapStart();

    ImGui_ImplDX12_Init(device,
                        NUM_BACK_BUFFERS,
                        DXGI_FORMAT_R8G8B8A8_UNORM,
                        imgui_srv_heap, //unused for now
                        srv_cpu_handle,
                        srv_gpu_handle);
    ImGui_ImplDX12_NewFrame();
}

void imgui_new_frame()
{
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void imgui_render(ID3D12GraphicsCommandList *cmd_list)
{
    cmd_list->SetDescriptorHeaps(1, &imgui_srv_heap);
    ImGui::Render();
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd_list);
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
void imgui_wndproc(UINT msg, WPARAM wParam, LPARAM lParam)
{
    ImGui_ImplWin32_WndProcHandler(g_hwnd, msg, wParam, lParam);
}

void imgui_shutdown()
{
    ImGui_ImplDX12_Shutdown();
    ImGui_ImplWin32_Shutdown();
    if (ImGui::GetCurrentContext())
    {
        ImGui::DestroyContext();
    }
    safe_release(imgui_srv_heap);
}

void imgui_app_combo()
{
    const char *demo_str[] = {"2D Transforms",
                              "3D Transforms",
                              "Particles"};
    int demo_index = (int)demo_to_show;
    const char *current_demo = demo_str[demo_index];
    if (ImGui::BeginCombo("Demo to show", current_demo))
    {
        for (int i = 0; i < _countof(demo_str); ++i)
        {
            bool is_selected = (current_demo == demo_str[i]);
            if (ImGui::Selectable(demo_str[i], is_selected))
            {
                current_demo = demo_str[i];
                demo_to_show = (demos)i;
                if (!is_selected)
                {
                    demo_changed = true;
                }
            }

            if (is_selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void imgui_pso_combo(int *view)
{
    const char *view_str[] = {"Flat color",
                              "Wireframe"};
    int view_index = *view;
    const char *current_view = view_str[view_index];
    if (ImGui::BeginCombo("View", current_view))
    {
        for (int i = 0; i < _countof(view_str); ++i)
        {
            bool is_selected = (current_view == view_str[i]);
            if (ImGui::Selectable(view_str[i], is_selected))
            {
                current_view = view_str[i];
                *view = i;
                if (!is_selected)
                {
                    // on changed side-effect
                }
            }

            if (is_selected)
            {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
}

void imgui_mouse_pos()
{
    mouse_pos.x = ImGui::GetMousePos().x;
    mouse_pos.y = ImGui::GetMousePos().y;
    ndc_mouse_pos.x = (2.f * mouse_pos.x / g_hwnd_width) - 1.f;
    ndc_mouse_pos.y = -(2.f * mouse_pos.y / g_hwnd_height) + 1.f;

    ImGui::Columns(3, "mouse_coords");
    ImGui::Separator();
    ImGui::Text("Mouse coords");
    ImGui::NextColumn();
    ImGui::Text("X");
    ImGui::NextColumn();
    ImGui::Text("Y");
    ImGui::NextColumn();
    ImGui::Separator();
    ImGui::Text("Screen");
    ImGui::Text("NDC");
    ImGui::NextColumn();

    char coords_buf[50];
    sprintf(coords_buf, "%f", mouse_pos.x);
    ImGui::Text(coords_buf); // screen space x

    sprintf(coords_buf, "%f", ndc_mouse_pos.x);
    ImGui::Text(coords_buf); // NDC space x
    ImGui::NextColumn();

    sprintf(coords_buf, "%f", mouse_pos.y);
    ImGui::Text(coords_buf); // screen space y

    sprintf(coords_buf, "%f", ndc_mouse_pos.y);
    ImGui::Text(coords_buf); // NDC space y

    ImGui::Columns(1);
    ImGui::Separator();
}

void imgui_gpu_memory(IDXGIAdapter4 *adapter)
{
    UINT64 local_usage = 0;
    UINT64 local_budget = 0;
    UINT64 nonlocal_usage = 0;
    UINT64 nonlocal_budget = 0;

    // Ideally we would query for the video memory info right after resetting the command list.
    DXGI_QUERY_VIDEO_MEMORY_INFO mem_info;
    adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &mem_info);
    local_usage = mem_info.CurrentUsage;
    local_budget = mem_info.Budget;

    adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_NON_LOCAL, &mem_info);
    nonlocal_usage = mem_info.CurrentUsage;
    nonlocal_budget = mem_info.Budget;

    ImGui::Text("Local (video) memory");
    ImGui::Indent(10.f);

    ImGui::Text("Current usage: %u", local_usage);
    ImGui::Text("Budget: %u", local_budget);
    ImGui::ProgressBar((float)local_usage / (float)local_budget, ImVec2(0.f, 0.f));
    ImGui::Unindent(10.f);

    ImGui::Text("Non-local (system) memory");
    ImGui::Indent(10.f);
    ImGui::Text("Current usage: %u", nonlocal_usage);
    ImGui::Text("Budget: %u", nonlocal_budget);
    ImGui::ProgressBar((float)nonlocal_usage / (float)nonlocal_budget, ImVec2(0.f, 0.f));
    ImGui::Unindent(10.f);
    ImGui::Separator();
}

bool is_hovering_window()
{
    return ImGui::IsAnyWindowHovered() || ImGui::IsAnyItemHovered() || ImGui::IsAnyItemFocused() || ImGui::IsAnyWindowFocused();
}