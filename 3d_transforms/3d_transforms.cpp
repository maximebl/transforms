#pragma once
#include "pch.h"
#include "../common/gpu_interface.h"
#include "../common/gpu_query.h"
#include "../common/imgui_helpers.h"
#include "frame_resource.h"
#include <vector>
#include <unordered_map>

using namespace DirectX;

extern "C" __declspec(dllexport) bool update_and_render();
extern "C" __declspec(dllexport) void resize(int width, int height);
extern "C" __declspec(dllexport) void cleanup();
extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) bool initialize();

internal void init_pipeline();
internal void create_default_pso();
internal void create_render_items();
internal void draw_render_items(ID3D12GraphicsCommandList *cmd_list, const std::vector<render_item> *render_items);

internal device_resources *dr = nullptr;
internal ID3D12Device *device = nullptr;
internal ID3D12GraphicsCommandList *cmd_list = nullptr;
internal ID3D12CommandAllocator *cmd_alloc = nullptr;

// PSOs
internal ID3D12PipelineState *flat_color_pso = nullptr;

// Shader data
internal ID3DBlob *perspective_blob_vs = nullptr;
internal ID3DBlob *perspective_blob_ps = nullptr;

// Queries data
#define NUM_QUERIES 5
internal gpu_query *query = nullptr;
internal double imgui_gpu_time = 0;

// Application state
internal bool is_vsync = true;
internal bool is_4x_msaa = false;
internal UINT msaa_quality = 0;
internal bool show_demo = true;
internal bool show_main = true;
internal bool show_debug = true;
internal bool show_perf = true;

// render items data
#define NUM_RENDER_ITEMS 3
struct position_color
{
    XMFLOAT3 position;
    XMFLOAT4 color;
};
internal ID3D12Resource *viewproj_upload = nullptr;
internal frame_resource *frame_resources[NUM_BACK_BUFFERS];
internal frame_resource *frame = nullptr;

std::unordered_map<const char *, mesh> geometries;
std::vector<render_item> render_items;

extern "C" __declspec(dllexport) bool initialize()
{
    if (!XMVerifyCPUSupport())
        return false;

    dr = new device_resources();
    device = dr->device;

    imgui_init(dr->device);

    if (!compile_shader(L"..\\..\\3d_transforms\\shaders\\perspective.hlsl", L"VS", shader_type::vertex, &perspective_blob_vs))
        return false;
    if (!compile_shader(L"..\\..\\3d_transforms\\shaders\\perspective.hlsl", L"PS", shader_type::pixel, &perspective_blob_ps))
        return false;

    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        frame_resources[i] = new frame_resource(device, i, NUM_RENDER_ITEMS);
    frame = frame_resources[0];

    hr = device->CreateCommandList(DEFAULT_NODE,
                                   D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   frame_resources[0]->cmd_alloc,
                                   nullptr,
                                   IID_PPV_ARGS(&cmd_list));
    ASSERT(SUCCEEDED(hr));

    query = new gpu_query(device, cmd_list, dr->cmd_queue, &dr->backbuffer_index, NUM_QUERIES);

    std::vector<CD3DX12_ROOT_PARAMETER1> params;

    // (root) ConstantBuffer<view_proj> cb_viewproj : register(b0);
    CD3DX12_ROOT_PARAMETER1 param_viewproj;
    param_viewproj.InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);
    params.push_back(param_viewproj);

    // (root) ConstantBuffer<object> cb_object : register(b1);
    CD3DX12_ROOT_PARAMETER1 param_obj;
    param_obj.InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);
    params.push_back(param_obj);

    dr->create_rootsig(&params, L"main_rootsig");
    create_default_pso();

    // create triangle data
    position_color vertices[3] = {
        {{0.0f, 0.25f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{0.25f, -0.25f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{-0.25f, -0.25f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    };

    const char *mesh_name = "triangle";
    mesh triangle;
    triangle.name = mesh_name;
    triangle.cb_index = 0;
    create_mesh_data(device, cmd_list, &triangle, sizeof(position_color), _countof(vertices), (void *)vertices, mesh_name);
    geometries[mesh_name] = triangle;

    create_render_items();

    cmd_list->Close();
    dr->cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&cmd_list);
    dr->flush_cmd_queue();

    return true;
}

internal void create_default_pso()
{
    D3D12_INPUT_ELEMENT_DESC input_elem_descs[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    D3D12_GRAPHICS_PIPELINE_STATE_DESC flat_pso_desc = {};
    flat_pso_desc.NodeMask = DEFAULT_NODE;
    flat_pso_desc.pRootSignature = dr->rootsig;
    flat_pso_desc.InputLayout = {input_elem_descs, _countof(input_elem_descs)};
    flat_pso_desc.VS = {perspective_blob_vs->GetBufferPointer(), perspective_blob_vs->GetBufferSize()};

    flat_pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC1(D3D12_DEFAULT);
    flat_pso_desc.DSVFormat = dr->dsv_format;

    flat_pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    flat_pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    flat_pso_desc.RTVFormats[0] = dr->rtv_format;
    flat_pso_desc.NumRenderTargets = 1;
    flat_pso_desc.SampleMask = UINT_MAX;
    flat_pso_desc.SampleDesc.Count = is_4x_msaa ? 4 : 1;
    flat_pso_desc.SampleDesc.Quality = is_4x_msaa ? (msaa_quality - 1) : 0;
    flat_pso_desc.PS = {perspective_blob_ps->GetBufferPointer(), perspective_blob_ps->GetBufferSize()};

    flat_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    hr = device->CreateGraphicsPipelineState(&flat_pso_desc, IID_PPV_ARGS(&flat_color_pso));
    ASSERT(SUCCEEDED(hr));
    return;
}

internal void create_render_items()
{
    render_item triangle_ri;
    triangle_ri.meshes = &geometries["triangle"];
    triangle_ri.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    triangle_ri.cb_index = 0;
    XMStoreFloat4x4(&triangle_ri.world, XMMatrixIdentity());
    render_items.push_back(triangle_ri);

    render_item triangle_ri2;
    triangle_ri2.meshes = &geometries["triangle"];
    triangle_ri2.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    triangle_ri2.cb_index = 1;
    XMStoreFloat4x4(&triangle_ri2.world, XMMatrixIdentity());
    render_items.push_back(triangle_ri2);
}

internal void draw_render_items(ID3D12GraphicsCommandList *cmd_list, const std::vector<render_item> *render_items)
{
    UINT cb_size = frame->cb_objconstants_size;
    UINT64 cb_resource_address = frame->cb_objconstant_upload->m_uploadbuffer->GetGPUVirtualAddress();

    for (render_item ri : *render_items)
    {
        UINT cb_offset = ri.cb_index * cb_size;

        cmd_list->IASetPrimitiveTopology(ri.topology);
        cmd_list->SetGraphicsRootConstantBufferView(1, cb_resource_address + cb_offset);
        cmd_list->IASetVertexBuffers(0, 1, &geometries["triangle"].vbv);
        cmd_list->DrawInstanced(3, 1, ri.base_vertex_location, 0);
    }
}

void imgui_update()
{
    imgui_new_frame();

    ImGui::Begin("3D Transforms", &show_main);
    imgui_appswitcher();
    ImGui::End();

    ImGui::Begin("Performance", &show_perf);
    imgui_gpu_memory(dr->adapter);
    ImGui::Text("imgui gpu time %.4f ms/frame",
                imgui_gpu_time);
    ImGui::End();

    ImGui::ShowDemoWindow(&show_demo);

    ImGui::Begin("Debug info", &show_debug);
    imgui_mouse_pos();
    ImGui::End();
}

extern "C" __declspec(dllexport) bool update_and_render()
{
    imgui_update();

    // frame resource synchronization
    dr->wait_present();
    UINT backbuffer_index = dr->swapchain->GetCurrentBackBufferIndex(); // gets updated after each call to Present()
    dr->backbuffer_index = backbuffer_index;

    frame = frame_resources[backbuffer_index];
    dr->cpu_wait(frame->fence_value);
    cmd_alloc = frame->cmd_alloc;

    // update object constant buffers
    for (int i = 0; i < render_items.size(); i++)
    {
        XMMATRIX world = XMLoadFloat4x4(&render_items[i].world);
        float amount = render_items[i].cb_index * 0.5f;
        XMMATRIX t = XMMatrixTranslation(amount, amount, amount);
        // apply matrix transforms
        world = XMMatrixMultiplyTranspose(world, t);

        //world = XMMatrixTranspose(world);
        XMFLOAT4X4 transposed;
        XMStoreFloat4x4(&transposed, world);

        frame->cb_objconstant_upload->copy_data(render_items[i].cb_index, (void *)&transposed);
    }

    // render
    cmd_alloc->Reset();
    cmd_list->Reset(cmd_alloc, flat_color_pso);

    cmd_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                     dr->main_rt_resources[backbuffer_index],
                                     D3D12_RESOURCE_STATE_PRESENT,
                                     D3D12_RESOURCE_STATE_RENDER_TARGET));

    set_viewport_rects(cmd_list);
    cmd_list->ClearDepthStencilView(dr->dsv_heap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.f, 0, 0, NULL);
    cmd_list->ClearRenderTargetView(dr->rtv_descriptors[backbuffer_index], Colors::IndianRed, 0, NULL);
    cmd_list->OMSetRenderTargets(1, &dr->rtv_descriptors[backbuffer_index], FALSE, &dr->dsv_heap->GetCPUDescriptorHandleForHeapStart());

    cmd_list->SetGraphicsRootSignature(dr->rootsig);
    draw_render_items(cmd_list, &render_items);

    query->start_query("ui_rendering");
    imgui_render(cmd_list);
    query->end_query("ui_rendering");

    cmd_list->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                     dr->main_rt_resources[backbuffer_index],
                                     D3D12_RESOURCE_STATE_RENDER_TARGET,
                                     D3D12_RESOURCE_STATE_PRESENT));

    query->resolve();
    cmd_list->Close();
    dr->cmd_queue->ExecuteCommandLists((UINT)1, (ID3D12CommandList *const *)&cmd_list);

    dr->present(is_vsync);
    frame->fence_value = dr->signal();

    imgui_gpu_time = query->result("ui_rendering");

    return true;
}

extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam)
{
    imgui_wndproc(msg, wParam, lParam);
}

extern "C" __declspec(dllexport) void resize(int width, int height)
{
    dr->wait_last_frame();
    ImGui_ImplDX12_InvalidateDeviceObjects();
    dr->resize(width, height);
    ImGui_ImplDX12_CreateDeviceObjects();
}

extern "C" __declspec(dllexport) void cleanup()
{
    dr->flush_cmd_queue();

    safe_release(flat_color_pso);
    safe_release(cmd_list);
    for (std::pair<const char *, mesh> geometry : geometries)
    {
        safe_release(geometry.second.default_resource);
        safe_release(geometry.second.upload_resource);
    }

    imgui_shutdown();

    for (size_t i = 0; i < _countof(frame_resources); i++)
        delete frame_resources[i];

    delete dr;
    delete query;

#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1 *debug = NULL;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
    {
        debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
        debug->Release();
    }
#endif
}
