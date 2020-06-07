#pragma once
#include "pch.h"
#include "../common/gpu_interface.h"
#include "../common/gpu_query.h"
#include "../common/imgui_helpers.h"
#include "frame_resource.h"
#include <vector>
#include <unordered_map>
#include <algorithm>

using namespace DirectX;

extern "C" __declspec(dllexport) bool update_and_render();
extern "C" __declspec(dllexport) void resize(int width, int height);
extern "C" __declspec(dllexport) void cleanup();
extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) bool initialize();

internal void init_pipeline();
internal void create_render_items();
internal void draw_render_items(ID3D12GraphicsCommandList *cmd_list, const std::vector<render_item> *render_items);

internal device_resources *dr = nullptr;
internal ID3D12Device *device = nullptr;
internal ID3D12GraphicsCommandList *cmd_list = nullptr;
internal ID3D12CommandAllocator *cmd_alloc = nullptr;
internal frame_resource *frame_resources[NUM_BACK_BUFFERS];
internal frame_resource *frame = nullptr;

// PSOs
enum views
{
    flat_color = 0,
    wireframe
};
internal views shading;
internal ID3D12PipelineState *flat_color_pso = nullptr;
internal ID3D12PipelineState *wireframe_pso = nullptr;

// Shader data
internal ID3DBlob *perspective_blob_vs = nullptr;
internal ID3DBlob *perspective_blob_ps = nullptr;

// Queries data
#define NUM_QUERIES 5
internal gpu_query *query = nullptr;
internal double imgui_gpu_time = 0;

// Application state
internal float bg_color[4] = {0.f, 0.f, 0.f, 1.f};
internal bool is_vsync = true;
internal bool show_demo = true;
internal bool show_main = true;
internal bool show_debug = true;
internal bool show_perf = true;

// Render items data
#define NUM_RENDER_ITEMS 1
#define MAX_INSTANCE_COUNT_PER_OBJECT 1
internal std::vector<render_item> render_items;

// Geometry
internal std::unordered_map<const wchar_t *, mesh> geometries;

// Camera
internal pass_data pass;
internal float theta = 1.5f * XM_PI; // 270 degrees (3 * PI) / 2
internal float phi = XM_PIDIV4;      // 45 degrees  (PI / 4)
internal float radius = 5.0f;
internal ImVec2 last_mouse_pos;

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
        frame_resources[i] = new frame_resource(device, i,
                                                NUM_RENDER_ITEMS,
                                                NUM_RENDER_ITEMS * MAX_INSTANCE_COUNT_PER_OBJECT,
                                                1);
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

    //(root) StructuredBuffer<instance> sb_instance : register(t0);
    CD3DX12_ROOT_PARAMETER1 param_inst;
    param_inst.InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);
    params.push_back(param_inst);

    dr->create_rootsig(&params, L"main_rootsig");

    std::vector<D3D12_INPUT_ELEMENT_DESC> input_elem_descs;
    input_elem_descs.push_back({"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    input_elem_descs.push_back({"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});

    D3D12_GRAPHICS_PIPELINE_STATE_DESC default_pso_desc = dr->create_default_pso(&input_elem_descs, perspective_blob_vs, perspective_blob_ps);
    default_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    hr = device->CreateGraphicsPipelineState(&default_pso_desc, IID_PPV_ARGS(&flat_color_pso));
    ASSERT(SUCCEEDED(hr));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframe_pso_desc = dr->create_default_pso(&input_elem_descs, perspective_blob_vs, perspective_blob_ps);
    wireframe_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    wireframe_pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    hr = device->CreateGraphicsPipelineState(&wireframe_pso_desc, IID_PPV_ARGS(&wireframe_pso));
    ASSERT(SUCCEEDED(hr));

    // create triangle data
    position_color triangle_vertices[12] = {
        {{0.5f, 0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
        {{-0.5f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    WORD triangle_indices[6] = {0, 1, 3,
                                1, 2, 3};
    const wchar_t *mesh_name = L"triangle";
    mesh triangle;
    triangle.name = mesh_name;
    create_mesh_data(device, cmd_list, mesh_name,
                     sizeof(position_color), _countof(triangle_vertices), (void *)triangle_vertices,
                     sizeof(WORD), _countof(triangle_indices), (void *)triangle_indices,
                     &triangle);
    geometries[mesh_name] = triangle;

    // create cube data
    position_color cube_vertices[8] = {
        {XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White)}, // 0
        {XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT4(Colors::White)},  // 1
        {XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT4(Colors::White)},   // 2
        {XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White)},  // 3
        {XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT4(Colors::White)},  // 4
        {XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT4(Colors::White)},   // 5
        {XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT4(Colors::White)},    // 6
        {XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT4(Colors::White)}    // 7
    };

    WORD cube_indices[36] =
        {
            // front face
            0, 1, 2,
            0, 2, 3,

            // back face
            4, 6, 5,
            4, 7, 6,

            // left face
            4, 5, 1,
            4, 1, 0,

            // right face
            3, 2, 6,
            3, 6, 7,

            // top face
            1, 5, 6,
            1, 6, 2,

            // bottom face
            4, 0, 3,
            4, 3, 7};

    mesh_name = L"cube";
    mesh cube;
    create_mesh_data(device, cmd_list, mesh_name,
                     sizeof(position_color), _countof(cube_vertices), (void *)cube_vertices,
                     sizeof(WORD), _countof(cube_indices), (void *)cube_indices,
                     &cube);
    geometries[mesh_name] = cube;

    // stanford bunny
    const char *model_file = "C:\\Users\\maxim\\source\\repos\\transforms\\3d_transforms\\models\\shovel.fbx";

    std::vector<position_color> bunny_vertices;
    std::vector<WORD> bunny_indices;
    std::vector<mesh_data> meshdata = import_meshdata(model_file);

    for (int i = 0; i < meshdata.size(); i++)
    {
        for (int j = 0; j < meshdata[i].vertices.size(); j++)
        {
            bunny_vertices.push_back(meshdata[i].vertices[j]);
        }

        for (int j = 0; j < meshdata[i].indices.size(); j++)
        {
            bunny_indices.push_back(meshdata[i].indices[j]);
        }
    }

    mesh_name = L"bunny";
    mesh bunny;
    create_mesh_data(device, cmd_list, mesh_name,
                     sizeof(position_color), bunny_vertices.size(), (void *)bunny_vertices.data(),
                     sizeof(WORD), bunny_indices.size(), (void *)bunny_indices.data(),
                     &bunny);
    geometries[mesh_name] = bunny;
    create_render_items();

    cmd_list->Close();
    dr->cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&cmd_list);
    dr->flush_cmd_queue();

    return true;
}

internal void create_render_items()
{
    for (int i = 0; i < NUM_RENDER_ITEMS; i++)
    {
        render_item ri;
        ri.meshes = &geometries[L"bunny"];
        ri.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        ri.cb_index = i;
        ri.vertex_count = ri.meshes->vertex_count;
        ri.index_count = ri.meshes->index_count;
        ri.instance_count = MAX_INSTANCE_COUNT_PER_OBJECT;
        XMStoreFloat4x4(&ri.world, XMMatrixIdentity());

        // create instance data
        for (int i = 0; i < MAX_INSTANCE_COUNT_PER_OBJECT; i++)
        {
            ri.instance_data.resize(MAX_INSTANCE_COUNT_PER_OBJECT);
            XMStoreFloat4x4(&ri.instance_data[i].world, XMMatrixIdentity());
        }
        render_items.push_back(ri);
    }
}

internal void draw_render_items(ID3D12GraphicsCommandList *cmd_list, const std::vector<render_item> *render_items)
{
    UINT cb_size = frame->cb_objconstants_size;
    UINT64 cb_resource_address = frame->cb_objconstant_upload->m_uploadbuffer->GetGPUVirtualAddress();

    for (render_item ri : *render_items)
    {
        cmd_list->IASetPrimitiveTopology(ri.topology);
        cmd_list->IASetVertexBuffers(0, 1, &ri.meshes->vbv);
        cmd_list->IASetIndexBuffer(&ri.meshes->ibv);

        cmd_list->SetGraphicsRootShaderResourceView(2, frame->sb_instancedata_upload->m_uploadbuffer->GetGPUVirtualAddress());

        UINT cb_offset = ri.cb_index * cb_size;
        cmd_list->SetGraphicsRootConstantBufferView(1, cb_resource_address + cb_offset);
        //cmd_list->DrawInstanced(ri.vertex_count, ri.instance_count, ri.start_index_location, ri.base_vertex_location);
        cmd_list->DrawIndexedInstanced(ri.index_count, ri.instance_count, ri.start_index_location, ri.base_vertex_location, 0);
    }
}

void imgui_update()
{
    imgui_new_frame();

    ImGui::Begin("3D Transforms", &show_main);
    imgui_app_combo();
    ImGui::End();

    ImGui::Begin("Performance", &show_perf);
    imgui_gpu_memory(dr->adapter);
    ImGui::Text("imgui gpu time %.4f ms/frame",
                imgui_gpu_time);
    ImGui::End();

    ImGui::ShowDemoWindow(&show_demo);

    ImGui::Begin("Debug info", &show_debug);
    imgui_mouse_pos();
    imgui_pso_combo((int *)&shading);
    ImGui::ColorEdit3("Background color", bg_color);
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

    // view matrix
    float x = radius * sinf(phi) * cosf(theta); // zero
    float y = radius * cosf(phi);               // 3.53
    float z = radius * sinf(phi) * sinf(theta); // -3.53

    XMVECTOR pos = XMVectorSet(x, y, z, 1.0f);
    XMVECTOR target = XMVectorZero();
    XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

    XMMATRIX view = XMMatrixTranspose(XMMatrixLookAtLH(pos, target, up));
    XMStoreFloat4x4(&pass.view, view);

    // projection matrix
    float aspect_ratio = (float)g_hwnd_width / (float)g_hwnd_height;
    XMMATRIX proj = XMMatrixTranspose(XMMatrixPerspectiveFovLH(0.25f * XM_PI, aspect_ratio, 1.0f, 1000.0f));
    XMStoreFloat4x4(&pass.proj, proj);

    frame->cb_passdata_upload->copy_data(0, (void *)&pass);

    // update object constant data
    for (int i = 0; i < render_items.size(); i++)
    {
        render_item ri = render_items[i];
        XMMATRIX world = XMLoadFloat4x4(&ri.world);
        float amount = ri.cb_index * 0.1f;
        XMMATRIX t = XMMatrixTranslation(amount, amount, amount);
        // apply matrix transforms
        world = XMMatrixMultiplyTranspose(world, t);
        XMFLOAT4X4 transposed;
        XMStoreFloat4x4(&transposed, world);

        frame->cb_objconstant_upload->copy_data(ri.cb_index, (void *)&transposed);

        // update instance data
        for (int j = 0; j < ri.instance_data.size(); j++)
        {
            instance_data instance = ri.instance_data[j];
            world = XMLoadFloat4x4(&instance.world);
            amount = 0.2f * j;
            t = XMMatrixTranslation(amount, amount, amount);
            world = XMMatrixMultiplyTranspose(world, t);
            XMStoreFloat4x4(&transposed, world);

            frame->sb_instancedata_upload->copy_data(j, (void *)&transposed);
        }
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
    cmd_list->ClearRenderTargetView(dr->rtv_descriptors[backbuffer_index], bg_color, 0, NULL);
    cmd_list->OMSetRenderTargets(1, &dr->rtv_descriptors[backbuffer_index], FALSE, &dr->dsv_heap->GetCPUDescriptorHandleForHeapStart());

    switch (shading)
    {
    case flat_color:
        cmd_list->SetPipelineState(flat_color_pso);
        break;

    case wireframe:
        cmd_list->SetPipelineState(wireframe_pso);
        break;

    default:
        break;
    }

    cmd_list->SetGraphicsRootSignature(dr->rootsig);
    cmd_list->SetGraphicsRootConstantBufferView(0, frame->cb_passdata_upload->m_uploadbuffer->GetGPUVirtualAddress());
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

    switch (msg)
    {
    case WM_MOUSEMOVE:
        if (!ImGui::IsAnyWindowHovered() && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemFocused() && !ImGui::IsAnyWindowFocused())
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {

                float x = ImGui::GetMousePos().x;
                float y = ImGui::GetMousePos().y;

                // 1 pixel = 0.25 degrees
                float dx = XMConvertToRadians(0.25f * (x - last_mouse_pos.x));
                float dy = XMConvertToRadians(0.25f * (y - last_mouse_pos.y));

                // Update camera angles
                theta -= dx;
                phi -= dy;

                // prevent wrap around
                phi = std::clamp<float>(phi, 0.1f, XM_PI - 0.1f);
            }
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right))
            {
                // Make each pixel correspond to 0.005 unit in the scene.
                float dx = 0.005f * static_cast<float>(ImGui::GetMousePos().x - last_mouse_pos.x);
                float dy = 0.005f * static_cast<float>(ImGui::GetMousePos().y - last_mouse_pos.y);

                // Update the camera radius based on input.
                radius += dx - dy;

                // Restrict the radius.
                radius = std::clamp(radius, 3.0f, 200.0f);
            }
        }
        last_mouse_pos.x = ImGui::GetMousePos().x;
        last_mouse_pos.y = ImGui::GetMousePos().y;
        break;

    case WM_RBUTTONDOWN:
        last_mouse_pos.x = ImGui::GetMousePos().x;
        last_mouse_pos.y = ImGui::GetMousePos().y;
        break;
    default:
        break;
    }
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
    safe_release(wireframe_pso);
    safe_release(cmd_list);
    for (std::pair<const wchar_t *, mesh> geometry : geometries)
    {
        safe_release(geometry.second.vertex_default_resource);
        safe_release(geometry.second.vertex_upload_resource);
        safe_release(geometry.second.index_default_resource);
        safe_release(geometry.second.index_upload_resource);
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
