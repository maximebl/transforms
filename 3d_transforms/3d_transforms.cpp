#pragma once
#include "pch.h"
#include <gpu_interface.h>
#include <gpu_query.h>
#include <imgui_helpers.h>
#include "frame_resource.h"
#include <vector>
#include <map>
#include <algorithm>
#include "shlwapi.h"
#include "math_helpers.h"
#include "camera.h"
#include <functional>

using namespace DirectX;

camera cam = camera((float)g_hwnd_width / (float)g_hwnd_height,
                    XMConvertToRadians(45.f),
                    XMVectorSet(0.f, 0.f, -10.f, 1.f));

extern "C" __declspec(dllexport) bool update_and_render();
extern "C" __declspec(dllexport) void resize(int width, int height);
extern "C" __declspec(dllexport) void cleanup();
extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) bool initialize();

s_internal void init_pipeline();
s_internal void create_render_item(std::string name, std::wstring mesh_name, int id);
s_internal void draw_render_items(ID3D12GraphicsCommandList *cmd_list, const std::vector<render_item> *render_items);
s_internal void update_camera();

// Command objects
s_internal device_resources *dr = nullptr;
s_internal ID3D12Device *device = nullptr;
s_internal ID3D12GraphicsCommandList *cmdlist = nullptr;
s_internal ID3D12GraphicsCommandList *ui_requests_cmdlist = nullptr;
s_internal ID3D12CommandAllocator *ui_requests_cmdalloc = nullptr;
s_internal ID3D12CommandAllocator *cmd_alloc = nullptr;
s_internal frame_resource *frame_resources[NUM_BACK_BUFFERS];
s_internal frame_resource *frame = nullptr;

// PSOs
enum views
{
    flat_color = 0,
    wireframe
};
s_internal views shading;
s_internal ID3D12PipelineState *flat_color_pso = nullptr;
s_internal ID3D12PipelineState *wireframe_pso = nullptr;
s_internal ID3D12PipelineState *stencil_pso = nullptr;
s_internal ID3D12PipelineState *outline_pso = nullptr;

// Shader data
s_internal ID3DBlob *perspective_blob_vs = nullptr;
s_internal ID3DBlob *perspective_blob_ps = nullptr;
s_internal ID3DBlob *outline_blob_ps = nullptr;
s_internal ID3DBlob *outline_blob_vs = nullptr;

// Queries data
#define NUM_QUERIES 5
s_internal gpu_query *query = nullptr;
s_internal double imgui_gpu_time = 0;

// Application state
s_internal float bg_color[4] = {0.f, 0.f, 0.f, 1.f};
s_internal bool is_vsync = true;
s_internal bool show_demo = true;
s_internal bool show_main = true;
s_internal bool show_debug = true;
s_internal bool show_perf = true;
s_internal bool show_selection = true;
s_internal float translation[3] = {};
s_internal float scale[3] = {1.f, 1.f, 1.f};
s_internal float right_angle = 0.f;
s_internal float up_angle = 0.f;
s_internal float forward_angle = 0.f;

// Render items data
#define MAX_RENDER_ITEMS 10
#define MAX_INSTANCE_COUNT_PER_OBJECT 2
s_internal std::vector<render_item> render_items;

// Instance data
s_internal int num_selected_instances = 0;
s_internal std::vector<instance *> total_ri_instances;
s_internal instance *selected_inst;
s_internal ID3D12DescriptorHeap *instanceIDs_srv_heap = nullptr;

// Geometry
s_internal std::unordered_map<std::wstring, mesh> geometries;

// Camera
s_internal pass_data pass;
//internal float radius = 5.0f;
s_internal ImVec2 last_mouse_pos;
s_internal float dx_angle = 0.f;
s_internal float dy_angle = 0.f;

bool is_item_picked = false;

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
    if (!compile_shader(L"..\\..\\3d_transforms\\shaders\\outline.hlsl", L"PS_outline", shader_type::pixel, &outline_blob_ps))
        return false;
    if (!compile_shader(L"..\\..\\3d_transforms\\shaders\\outline.hlsl", L"VS_outline", shader_type::vertex, &outline_blob_vs))
        return false;

    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        frame_resources[i] = new frame_resource(device, i,
                                                MAX_RENDER_ITEMS,
                                                MAX_RENDER_ITEMS * MAX_INSTANCE_COUNT_PER_OBJECT);
    frame = frame_resources[0];

    hr = device->CreateCommandList(DEFAULT_NODE,
                                   D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   frame_resources[0]->cmd_alloc,
                                   nullptr,
                                   IID_PPV_ARGS(&cmdlist));
    ASSERT(SUCCEEDED(hr));

    hr = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&ui_requests_cmdalloc));
    ASSERT(SUCCEEDED(hr));
    ui_requests_cmdalloc->SetName(L"ui_requests_cmdalloc");

    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc;
    srv_heap_desc.NodeMask = DEFAULT_NODE;
    srv_heap_desc.NumDescriptors = MAX_RENDER_ITEMS;
    srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    hr = device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&instanceIDs_srv_heap));
    ASSERT(SUCCEEDED(hr));

    hr = device->CreateCommandList(DEFAULT_NODE,
                                   D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   ui_requests_cmdalloc,
                                   nullptr,
                                   IID_PPV_ARGS(&ui_requests_cmdlist));
    ASSERT(SUCCEEDED(hr));
    ui_requests_cmdlist->SetName(L"ui_requests_cmdlist");
    ui_requests_cmdlist->Close();

    query = new gpu_query(device, cmdlist, dr->cmd_queue, &dr->backbuffer_index, NUM_QUERIES);

    std::vector<CD3DX12_ROOT_PARAMETER1> params;
    // (root) ConstantBuffer<view_proj> cb_viewproj : register(b0);
    CD3DX12_ROOT_PARAMETER1 param_viewproj;
    param_viewproj.InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);
    params.push_back(param_viewproj);

    // (root) ConstantBuffer<object> cb_object : register(b1);
    CD3DX12_ROOT_PARAMETER1 param_obj;
    param_obj.InitAsConstantBufferView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);
    params.push_back(param_obj);

    // (root) StructuredBuffer<instance> sb_instance : register(t0);
    CD3DX12_ROOT_PARAMETER1 param_inst;
    param_inst.InitAsShaderResourceView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);
    params.push_back(param_inst);

    // (root) StructuredBuffer<uint> cb_instance_ids : register(t1);
    CD3DX12_ROOT_PARAMETER1 param_inst_id;
    CD3DX12_DESCRIPTOR_RANGE1 range[1];
    range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, MAX_RENDER_ITEMS, 2, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);
    param_inst_id.InitAsDescriptorTable(1, range);
    params.push_back(param_inst_id);

    // (root) StructuredBuffer<uint> sb_selected_instance_ids : register(t2);
    CD3DX12_ROOT_PARAMETER1 param_selected_inst_ids;
    param_selected_inst_ids.InitAsShaderResourceView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);
    params.push_back(param_selected_inst_ids);

    // (root) ConstantBuffer<uint> cb_selected_insts_size : register(b2);
    CD3DX12_ROOT_PARAMETER1 param_selected_inst_size;
    param_selected_inst_size.InitAsConstants(1, 2, 0, D3D12_SHADER_VISIBILITY_VERTEX);
    params.push_back(param_selected_inst_size);

    dr->create_rootsig(&params, L"main_rootsig");

    std::vector<D3D12_INPUT_ELEMENT_DESC> input_elem_descs;
    input_elem_descs.push_back({"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    input_elem_descs.push_back({"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});

    D3D12_GRAPHICS_PIPELINE_STATE_DESC default_pso_desc = dr->create_default_pso_desc(&input_elem_descs);
    default_pso_desc.VS = {perspective_blob_vs->GetBufferPointer(), perspective_blob_vs->GetBufferSize()};
    default_pso_desc.PS = {perspective_blob_ps->GetBufferPointer(), perspective_blob_ps->GetBufferSize()};
    default_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    hr = device->CreateGraphicsPipelineState(&default_pso_desc, IID_PPV_ARGS(&flat_color_pso));
    ASSERT(SUCCEEDED(hr));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframe_pso_desc = default_pso_desc;
    wireframe_pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    hr = device->CreateGraphicsPipelineState(&wireframe_pso_desc, IID_PPV_ARGS(&wireframe_pso));
    ASSERT(SUCCEEDED(hr));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC stencil_pso_desc = default_pso_desc;
    stencil_pso_desc.DepthStencilState = create_stencil_dss();
    hr = device->CreateGraphicsPipelineState(&stencil_pso_desc, IID_PPV_ARGS(&stencil_pso));
    ASSERT(SUCCEEDED(hr));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC outline_pso_desc = default_pso_desc;
    outline_pso_desc.VS = {outline_blob_vs->GetBufferPointer(), outline_blob_vs->GetBufferSize()};
    outline_pso_desc.PS = {outline_blob_ps->GetBufferPointer(), outline_blob_ps->GetBufferSize()};
    outline_pso_desc.DepthStencilState = create_outline_dss();
    hr = device->CreateGraphicsPipelineState(&outline_pso_desc, IID_PPV_ARGS(&outline_pso));
    ASSERT(SUCCEEDED(hr));

    //create triangle data
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
    create_mesh_data(device, cmdlist, mesh_name,
                     sizeof(position_color), _countof(triangle_vertices), (void *)triangle_vertices,
                     sizeof(WORD), _countof(triangle_indices), (void *)triangle_indices,
                     &triangle);
    geometries[mesh_name] = triangle;

    // create cube data
    position_color cube_vertices[8] = {
        {XMFLOAT3(-1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::White)},         // 0
        {XMFLOAT3(-1.0f, 1.0f, -1.0f), XMFLOAT4(Colors::ForestGreen)},    // 1
        {XMFLOAT3(1.0f, 1.0f, -1.0f), XMFLOAT4(Colors::Aquamarine)},      // 2
        {XMFLOAT3(1.0f, -1.0f, -1.0f), XMFLOAT4(Colors::BlanchedAlmond)}, // 3
        {XMFLOAT3(-1.0f, -1.0f, 1.0f), XMFLOAT4(Colors::DarkKhaki)},      // 4
        {XMFLOAT3(-1.0f, 1.0f, 1.0f), XMFLOAT4(Colors::Cyan)},            // 5
        {XMFLOAT3(1.0f, 1.0f, 1.0f), XMFLOAT4(Colors::PaleVioletRed)},    // 6
        {XMFLOAT3(1.0f, -1.0f, 1.0f), XMFLOAT4(Colors::Beige)}            // 7
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
    create_mesh_data(device, cmdlist, mesh_name,
                     sizeof(position_color), _countof(cube_vertices), (void *)cube_vertices,
                     sizeof(WORD), _countof(cube_indices), (void *)cube_indices,
                     &cube);
    geometries[mesh_name] = cube;

    // stanford bunny
    const char *model_file = "C:\\Users\\maxim\\source\\repos\\transforms\\3d_transforms\\models\\bunny.obj";

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

    //mesh_name = L"bunny";
    //mesh bunny;
    //create_mesh_data(device, cmdlist, mesh_name,
    //                 sizeof(position_color), bunny_vertices.size(), (void *)bunny_vertices.data(),
    //                 sizeof(WORD), bunny_indices.size(), (void *)bunny_indices.data(),
    //                 &bunny);
    //geometries[mesh_name] = bunny;

    //for (int i = 0; i < NUM_RENDER_ITEMS; i++)
    //{
    //    create_render_item("my cool bunny", mesh_name, i);
    //}

    cmdlist->Close();
    dr->cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&cmdlist);
    dr->flush_cmd_queue();
    return true;
}

s_internal void create_render_item(std::string name, std::wstring mesh_name, int id)
{
    render_item ri;
    ri.meshes = geometries[mesh_name];
    ri.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    ri.cb_index = id;
    ri.vertex_count = ri.meshes.vertex_count;
    ri.index_count = ri.meshes.index_count;
    ri.name = name;
    XMMATRIX world = XMMatrixIdentity();
    XMMATRIX t = XMMatrixTranslation(0.f, 0.f, 0.f);
    world = world * t;
    XMStoreFloat4x4(&ri.world, world);

    // create instance data
    for (int i = 0; i < MAX_INSTANCE_COUNT_PER_OBJECT; i++)
    {
        instance inst;
        inst.name = "instance_" + std::to_string(i);
        XMMATRIX world = XMMatrixIdentity();

        XMVECTOR right = XMVectorSet(1.f, 0.f, 0.f, 0.f);
        inst.right_angle = 0.f;
        XMVECTOR right_rot_quat = XMQuaternionRotationAxis(right, XMConvertToRadians(inst.right_angle));

        XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
        inst.up_angle = 0.f;
        XMVECTOR up_rot_quat = XMQuaternionRotationAxis(up, XMConvertToRadians(inst.up_angle));

        XMVECTOR forward = XMVectorSet(0.f, 0.f, 1.f, 0.f);
        inst.forward_angle = 0.f;
        XMVECTOR forward_rot_quat = XMQuaternionRotationAxis(forward, XMConvertToRadians(inst.forward_angle));

        XMVECTOR rot_quat = XMQuaternionMultiply(forward_rot_quat, XMQuaternionMultiply(right_rot_quat, up_rot_quat));
        XMMATRIX rot_mat = XMMatrixRotationQuaternion(rot_quat);

        inst.scale[0] = 1.f;
        inst.scale[1] = 1.f;
        inst.scale[2] = 1.f;
        XMMATRIX scale_mat = XMMatrixScaling(inst.scale[0], inst.scale[1], inst.scale[2]);

        inst.translation[0] = 0.f;
        inst.translation[1] = 0.f;
        inst.translation[2] = 0.f;
        XMMATRIX translation_mat = XMMatrixTranslation(inst.translation[0], inst.translation[1], inst.translation[2]);
        XMMATRIX srt = scale_mat * rot_mat * translation_mat;
        world = world * srt;

        XMStoreFloat4x4(&inst.shader_data.world, world);
        inst.bounds = ri.meshes.bounds;
        ri.instances.push_back(inst);
    }

    // Create Instance IDs StructuredBuffer
    D3D12_BUFFER_SRV buffer_srv_desc;
    buffer_srv_desc.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    buffer_srv_desc.NumElements = MAX_INSTANCE_COUNT_PER_OBJECT;
    buffer_srv_desc.StructureByteStride = sizeof(UINT);
    buffer_srv_desc.FirstElement = ri.cb_index * MAX_INSTANCE_COUNT_PER_OBJECT;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc;
    srv_desc.Buffer = buffer_srv_desc;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv_desc.Format = DXGI_FORMAT_UNKNOWN;

    size_t offset = ri.cb_index * dr->srv_desc_handle_incr_size;
    D3D12_CPU_DESCRIPTOR_HANDLE offset_handle;
    offset_handle.ptr = instanceIDs_srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + offset;
    device->CreateShaderResourceView(frame->sb_instanceIDs_upload->m_uploadbuffer, &srv_desc, offset_handle);

    render_items.push_back(ri);
}

s_internal void draw_render_items(ID3D12GraphicsCommandList *cmd_list, const std::vector<render_item> *render_items)
{
    size_t cb_size = frame->cb_objconstants_size;
    size_t cb_resource_address = frame->cb_objconstant_upload->m_uploadbuffer->GetGPUVirtualAddress();

    for (render_item ri : *render_items)
    {
        cmd_list->IASetPrimitiveTopology(ri.topology);
        cmd_list->IASetVertexBuffers(0, 1, &ri.meshes.resource->vbv);
        cmd_list->IASetIndexBuffer(&ri.meshes.resource->ibv);

        cmd_list->SetGraphicsRootShaderResourceView(2, frame->sb_instancedata_upload->m_uploadbuffer->GetGPUVirtualAddress());

        size_t offset = ri.cb_index * dr->srv_desc_handle_incr_size;
        D3D12_GPU_DESCRIPTOR_HANDLE offset_handle;
        offset_handle.ptr = instanceIDs_srv_heap->GetGPUDescriptorHandleForHeapStart().ptr + offset;
        cmd_list->SetGraphicsRootDescriptorTable(3, offset_handle);

        size_t cb_offset = ri.cb_index * cb_size;
        cmd_list->SetGraphicsRootConstantBufferView(1, cb_resource_address + cb_offset);

        // Number of selected instances
        cmdlist->SetGraphicsRoot32BitConstant(5, (UINT)num_selected_instances, 0);

        // List of selected instance IDs
        cmd_list->SetGraphicsRootShaderResourceView(4, frame->sb_selected_instanceIDs_upload->m_uploadbuffer->GetGPUVirtualAddress());

        // Write to stencil buffer
        cmd_list->OMSetStencilRef(1);
        cmd_list->SetPipelineState(stencil_pso);
        cmd_list->DrawIndexedInstanced(ri.index_count, (UINT)ri.instances.size(), ri.start_index_location, ri.base_vertex_location, 0);

        if (ri.is_selected)
        {
            // Draw scaled up outline
            cmd_list->SetPipelineState(outline_pso);
            cmd_list->DrawIndexedInstanced(ri.index_count, (UINT)ri.instances.size(), ri.start_index_location, ri.base_vertex_location, 0);
        }
        // Draw mesh
        cmd_list->DrawIndexedInstanced(ri.index_count, (UINT)ri.instances.size(), ri.start_index_location, ri.base_vertex_location, 0);
    }
}

DWORD __stdcall select_and_load_model(void *param)
{
    ui_requests_cmdalloc->Reset();
    ui_requests_cmdlist->Reset(ui_requests_cmdalloc, nullptr);

    OPENFILENAME ofn;
    TCHAR szFile[MAX_PATH] = {};

    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = g_hwnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = _T("All\0*.*\0Text\0*.obj\0");
    ofn.nFilterIndex = 1;
    ofn.lpstrFileTitle = NULL;
    ofn.nMaxFileTitle = 0;
    ofn.lpstrInitialDir = NULL;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileName(&ofn) == TRUE)
    {
        char model_file[MAX_PATH];
        wcstombs(model_file, ofn.lpstrFile, MAX_PATH);

        std::vector<position_color> mesh_vertices;
        std::vector<WORD> mesh_indices;
        std::vector<mesh_data> meshdata = import_meshdata(model_file);
        std::vector<submesh> submeshes;

        XMVECTOR max_point = XMVectorZero();
        XMVECTOR min_point = XMVectorZero();

        for (int i = 0; i < meshdata.size(); i++)
        {
            submesh new_submesh;
            new_submesh.name = meshdata[i].name;
            new_submesh.vertex_count = (UINT)meshdata[i].vertices.size();
            new_submesh.index_count = (UINT)meshdata[i].indices.size();

            for (int j = 0; j < meshdata[i].vertices.size(); j++)
            {
                mesh_vertices.push_back(meshdata[i].vertices[j]);
                XMFLOAT3 position = meshdata[i].vertices[j].position;

                XMVECTOR point = XMLoadFloat3(&position);
                min_point = XMVectorMin(min_point, point);
                max_point = XMVectorMax(max_point, point);
            }

            new_submesh.bounds.CreateFromPoints(new_submesh.bounds, min_point, max_point);
            submeshes.push_back(new_submesh);

            for (int j = 0; j < meshdata[i].indices.size(); j++)
            {
                mesh_indices.push_back(meshdata[i].indices[j]);
            }
        }

        PathRemoveExtensionW(ofn.lpstrFile);
        wchar_t *mesh_name = PathFindFileNameW(ofn.lpstrFile);
        mesh new_mesh;
        new_mesh.submeshes = submeshes;
        new_mesh.bounds.CreateFromPoints(new_mesh.bounds, min_point, max_point);

        create_mesh_data(device, ui_requests_cmdlist, mesh_name,
                         sizeof(position_color), mesh_vertices.size(), (void *)mesh_vertices.data(),
                         sizeof(WORD), mesh_indices.size(), (void *)mesh_indices.data(),
                         &new_mesh);

        UINT id = (UINT)render_items.size();
        wchar_t id_str[12];
        wsprintf(id_str, L"_%u", id);
        StrCatW(mesh_name, id_str);
        char ri_name[MAX_PATH];
        wcstombs(ri_name, mesh_name, 100);

        geometries[mesh_name] = new_mesh;
        create_render_item(ri_name, mesh_name, id);

        ui_requests_cmdlist->Close();
        dr->cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&ui_requests_cmdlist);
    }
    return 0;
}

s_internal void update_camera()
{
    ImGui::SliderFloat4("Right direction", cam.right.m128_f32, -1.f, 1.f);
    ImGui::SameLine();
    if (ImGui::Button("Reset right direction"))
        cam.right = XMVectorSet(1.f, 0.f, 0.f, 0.f);

    ImGui::SliderFloat4("Up direction", cam.up.m128_f32, -1.f, 1.f);
    ImGui::SameLine();
    if (ImGui::Button("Reset up direction"))
        cam.up = XMVectorSet(0.f, 1.f, 0.f, 0.f);

    ImGui::SliderFloat4("Forward direction", cam.forward.m128_f32, -1.f, 1.f);
    ImGui::SameLine();
    if (ImGui::Button("Reset forward direction"))
        cam.forward = XMVectorSet(0.f, 0.f, 1.f, 0.f);

    ImGui::SliderFloat4("Camera position", cam.position.m128_f32, -150.f, 150.f);
    ImGui::SameLine();
    if (ImGui::Button("Reset camera position"))
    {
        cam.position = cam.m_start_pos;
    }

    ImGui::SliderFloat("Camera roll", &cam.roll, -180.f, +180.f);
    ImGui::SliderFloat("Camera pitch", &cam.pitch, -180.f, +180.f);
    ImGui::SliderFloat("Camera yaw", &cam.yaw, -180.f, +180.f);

    cam.forward = XMVector3Normalize(cam.forward);

    for (render_item &ri : render_items)
    {
        for (instance &inst : ri.instances)
            if (inst.is_selected == true)
            {
                cam.orbit_target_pos.m128_f32[0] = inst.shader_data.world._41;
                cam.orbit_target_pos.m128_f32[1] = inst.shader_data.world._42;
                cam.orbit_target_pos.m128_f32[2] = inst.shader_data.world._43;
                cam.orbit_target_pos.m128_f32[3] = inst.shader_data.world._44;
                ImGui::SliderFloat4("Orbit target position", cam.orbit_target_pos.m128_f32, -50.f, 50.f);
            }
    }

    XMVECTOR s = XMVectorReplicate(0.02f);
    if (GetAsyncKeyState('E'))
    {
        cam.position = XMVectorMultiplyAdd(-s, cam.up, cam.position);
    }

    if (GetAsyncKeyState('Q'))
    {
        cam.position = XMVectorMultiplyAdd(s, cam.up, cam.position);
    }

    if (GetAsyncKeyState('W'))
    {
        cam.position = XMVectorMultiplyAdd(s, cam.forward, cam.position);
    }
    if (GetAsyncKeyState('S'))
    {
        cam.position = XMVectorMultiplyAdd(-s, cam.forward, cam.position);
    }
    if (GetAsyncKeyState('A'))
    {
        cam.position = XMVectorMultiplyAdd(-s, cam.right, cam.position);
    }
    if (GetAsyncKeyState('D'))
    {
        cam.position = XMVectorMultiplyAdd(s, cam.right, cam.position);
    }

    //XMMATRIX view = T;
    //XMStoreFloat4x4(&pass.view, view);

    // projection matrix
    //float aspect_ratio = (float)g_hwnd_width / (float)g_hwnd_height;
    //XMMATRIX proj = XMMatrixTranspose(XMMatrixPerspectiveFovLH(0.25f * XM_PI, aspect_ratio, 1.0f, 1000.0f));
    //XMStoreFloat4x4(&pass.proj, proj);

    cam.update_view();
    XMStoreFloat4x4(&pass.view, cam.view);
    XMStoreFloat4x4(&pass.proj, XMMatrixTranspose(cam.proj));
    frame->cb_passdata_upload->copy_data(0, (void *)&pass);
}

struct tree_item
{
    bool *is_selected;
    std::string name;
    tree_item *parent;
    std::map<std::string, std::vector<tree_item>> children;
    std::function<void(tree_item *)> on_expand_callback;
    std::function<void(tree_item *)> on_select_callback;
};
std::vector<tree_item> selected_items;

// recursive function to handle nested tree items
void expand_nested(tree_item ti)
{
    bool is_expanded = ImGui::TreeNodeExV((void *)nullptr, ImGuiTreeNodeFlags_FramePadding, "", nullptr);
    ImGui::SameLine();
    if (ImGui::Selectable(ti.name.c_str(), *ti.is_selected))
    {
        ti.on_select_callback(&ti);
    }

    if (is_expanded)
    {
        ti.on_expand_callback(&ti); // render the ImGui content for this tree item
        ImGui::TreePop();
    }
}

void imgui_nested_tree()
{
    if (ImGui::CollapsingHeader("Render items"))
    {
        // Render items
        for (size_t i = 0; i < render_items.size(); i++)
        {
            render_item *ri = &render_items[i];

            ImGui::PushID((int)i);
            bool is_expanded = ImGui::TreeNodeExV((void *)nullptr, ImGuiTreeNodeFlags_FramePadding, "", nullptr);
            ImGui::SameLine();
            if (ImGui::Selectable(ri->name.c_str(), ri->is_selected))
            {
                if (!ImGui::GetIO().KeyCtrl)
                {
                    for (render_item &ri : render_items)
                    {
                        ri.is_selected = false;
                        for (instance &ri_inst : ri.instances)
                        {
                            ri_inst.is_selected = false;
                        }
                    }
                }
                ri->is_selected = true;
                for (instance &inst : ri->instances)
                {
                    inst.is_selected = true;
                }
            }

            if (is_expanded)
            {
                // Instances
                for (size_t j = 0; j < ri->instances.size(); j++)
                {
                    instance &inst = ri->instances[j];

                    ImGui::PushID((int)j);
                    bool is_expanded = ImGui::TreeNodeExV((void *)nullptr, ImGuiTreeNodeFlags_FramePadding, "", nullptr);
                    ImGui::SameLine();
                    if (ImGui::Selectable(inst.name.c_str(), inst.is_selected))
                    {
                        if (!ImGui::GetIO().KeyCtrl)
                        {
                            for (render_item &ri : render_items)
                            {
                                ri.is_selected = false;
                                for (instance &ri_inst : ri.instances)
                                {
                                    ri_inst.is_selected = false;
                                }
                            }
                        }
                        inst.is_selected = true;
                        ri->is_selected = true;
                    }
                    if (is_expanded)
                    {
                        ImGui::SliderFloat3("Translation", inst.translation, -20.f, 20.f);
                        ImGui::SameLine();
                        if (ImGui::Button("Reset translation"))
                        {
                            inst.translation[0] = 0.f;
                            inst.translation[1] = 0.f;
                            inst.translation[2] = 0.f;
                        }

                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }

                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Render items old"))
    {
        float indentation = 10.f;
        for (size_t i = 0; i < render_items.size(); i++)
        {
            render_item *ri = &render_items[i];
            std::vector<tree_item> submesh_children;
            tree_item tree_ri;

            // Submeshes
            size_t num_submeshes = ri->meshes.submeshes.size();
            for (size_t k = 0; k < num_submeshes; k++)
            {
                submesh *sm = &ri->meshes.submeshes[k];

                tree_item tree_sm;
                tree_sm.name = sm->name;
                tree_sm.is_selected = &sm->is_selected;
                tree_sm.parent = &tree_ri;

                tree_sm.on_expand_callback = [indentation, k, sm](tree_item *ti) {
                    ImGui::Indent(indentation);
                    ImGui::Text("Vertex count: %d", sm->vertex_count);
                    ImGui::Text("Index count: %d", sm->index_count);
                    ImGui::Unindent(indentation);
                };

                tree_sm.on_select_callback = [](tree_item *ti) {
                    if (!ImGui::GetIO().KeyCtrl)
                    {
                        for (size_t i = 0; i < render_items.size(); i++)
                        {
                            render_items[i].is_selected = false;
                            for (size_t j = 0; j < render_items[i].meshes.submeshes.size(); j++)
                            {
                                render_items[i].meshes.submeshes[j].is_selected = false;
                            }
                        }
                        selected_items.clear();
                    }

                    *ti->parent->is_selected = true;
                    selected_items.push_back(*ti->parent);

                    *ti->is_selected = true;
                    selected_items.push_back(*ti);
                };

                submesh_children.push_back(tree_sm);
            }

            // Instances
            std::vector<tree_item> instance_children;
            size_t num_instances = ri->instances.size();
            for (instance &inst : ri->instances)
            {
                tree_item tree_inst;
                tree_inst.parent = &tree_ri;
                tree_inst.name = inst.name;
                tree_inst.is_selected = &inst.is_selected;
                tree_inst.on_expand_callback = [inst](tree_item *ti) {
                    float inst_pos[4];
                    inst_pos[0] = inst.shader_data.world._41;
                    inst_pos[1] = inst.shader_data.world._42;
                    inst_pos[2] = inst.shader_data.world._43;
                    inst_pos[3] = inst.shader_data.world._44;
                    ImGui::InputFloat4("Position", inst_pos, 3);
                };

                tree_inst.on_select_callback = [ri](tree_item *ti) {
                    if (!ImGui::GetIO().KeyCtrl)
                    {
                        for (instance &inst : ri->instances)
                        {
                            inst.is_selected = false;
                        }
                        selected_items.clear();
                    }

                    *ti->is_selected = true;
                    selected_items.push_back(*ti);
                };
                instance_children.push_back(tree_inst);
            }

            // Render items
            tree_ri.name = ri->name;
            tree_ri.is_selected = &ri->is_selected;

            tree_ri.on_expand_callback = [num_instances, num_submeshes, indentation, ri, i](tree_item *ti) {
                int push_id = 0;

                ImGui::Text("render item #%zu", i);
                ImGui::Text("Instance count: %d", ri->instances.size());
                ImGui::Text("Vertex total: %d", ri->vertex_count);
                ImGui::Text("Index total: %d", ri->index_count);
                ImGui::NewLine();

                if (num_instances > 0)
                {
                    ImGui::Text("Instances (%zu)", num_instances);

                    std::map<std::string, std::vector<tree_item>> childrens = ti->children;
                    std::vector<tree_item> instances = childrens["instances"];
                    for (auto &tree_inst : instances)
                    {
                        ImGui::PushID(push_id++);
                        expand_nested(tree_inst);
                        ImGui::PopID();
                    }
                }

                ImGui::NewLine();

                if (num_submeshes > 0)
                {
                    ImGui::Text("Submeshes (%zu)", num_submeshes);
                    std::map<std::string, std::vector<tree_item>> childrens = ti->children;
                    std::vector<tree_item> submeshes = childrens["submeshes"];
                    for (auto &tree_sm : submeshes)
                    {
                        ImGui::PushID(push_id++);
                        expand_nested(tree_sm);
                        ImGui::PopID();
                    }
                }
            };

            tree_ri.on_select_callback = [ri](tree_item *ti) {
                // Clear render item and their submeshes selection when CTRL is not held
                if (!ImGui::GetIO().KeyCtrl)
                {
                    for (size_t i = 0; i < render_items.size(); i++)
                    {
                        render_items[i].is_selected = false;
                        for (size_t j = 0; j < render_items[i].meshes.submeshes.size(); j++)
                        {
                            render_items[i].meshes.submeshes[j].is_selected = false;
                        }
                    }
                    selected_items.clear();
                }

                // Select all child items when selecting a render item
                for (tree_item &child : ti->children["submeshes"])
                {
                    *child.is_selected = true;
                    selected_items.push_back(child);
                }
                *ti->is_selected = true;
                selected_items.push_back(*ti);
            };

            tree_ri.children["submeshes"] = submesh_children;
            tree_ri.children["instances"] = instance_children;

            ImGui::PushID((int)i);
            expand_nested(tree_ri);
            ImGui::PopID();
        }
    }
}

void imgui_update()
{
    if (GetAsyncKeyState(VK_ESCAPE))
    {
        cam.orbit_target_pos = cam.default_orbit_target;

        for (render_item &ri : render_items)
        {
            ri.is_selected = false;
            for (instance &inst : ri.instances)
                inst.is_selected = false;
        }
    }

    if (GetAsyncKeyState(VK_ESCAPE))
    {
        for (tree_item &item : selected_items)
        {
            *item.is_selected = false;
            selected_items.clear();
        }
    }

    imgui_new_frame();

    ImGui::Begin("3D Transforms", &show_main);
    imgui_app_combo();

    if (ImGui::Button("Load model"))
    {
        HANDLE ui_request_handle = CreateThread(nullptr, 0, select_and_load_model, nullptr, 0, nullptr);
        CloseHandle(ui_request_handle);
    }

    imgui_nested_tree();
    ImGui::End();

    ImGui::Begin("Selection", &show_selection);
    if (num_selected_instances > 0)
    {
        ImGui::SliderFloat3("Translation", translation, -20.f, 20.f);
        ImGui::SameLine();
        if (ImGui::Button("Reset translation"))
        {
            translation[0] = 0.f;
            translation[1] = 0.f;
            translation[2] = 0.f;
        }
        ImGui::SliderFloat("Right angle", &right_angle, 0, 360);
        ImGui::SameLine();
        if (ImGui::Button("Reset right angle"))
        {
            right_angle = 0.f;
        }
        ImGui::SliderFloat("Up angle", &up_angle, 0, 360);
        ImGui::SameLine();
        if (ImGui::Button("Reset up angle"))
        {
            up_angle = 0.f;
        }
        ImGui::SliderFloat("Forward angle", &forward_angle, 0, 360);
        ImGui::SameLine();
        if (ImGui::Button("Reset forward angle"))
        {
            forward_angle = 0.f;
        }

        ImGui::DragFloat3("Scale", scale, 0.01f, -20.f, 20.f);
        ImGui::SameLine();
        if (ImGui::Button("Reset scale"))
        {
            scale[0] = 1.f;
            scale[1] = 1.f;
            scale[2] = 1.f;
        }
    }

    ImGui::Separator();
    ImGui::SliderFloat("dx angle", &dx_angle, 0, 360);
    ImGui::SliderFloat("dy angle", &dy_angle, 0, 360);
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
    update_camera();

    total_ri_instances.clear();
    // update object constant data
    for (render_item &ri : render_items)
    {
        XMMATRIX world = XMLoadFloat4x4(&ri.world);
        XMFLOAT4X4 transposed;
        XMStoreFloat4x4(&transposed, XMMatrixTranspose(world));
        frame->cb_objconstant_upload->copy_data(ri.cb_index, (void *)&transposed);

        for (instance &inst : ri.instances)
        {
            total_ri_instances.push_back(&inst);
        }
    }

    // update instance data
    num_selected_instances = 0;
    frame->sb_selected_instanceIDs_upload->clear_data();

    for (size_t i = 0; i < total_ri_instances.size(); i++)
    {
        frame->sb_instanceIDs_upload->copy_data((int)i, (void *)&i);

        if (total_ri_instances[i]->is_selected)
        {
            frame->sb_selected_instanceIDs_upload->copy_data(num_selected_instances++, (void *)&i);
        }

        instance *inst = total_ri_instances[i];
        XMMATRIX inst_world = XMLoadFloat4x4(&inst->shader_data.world);
        instance_data inst_shader_data;
        XMStoreFloat4x4(&inst_shader_data.world, XMMatrixTranspose(inst_world));

        XMVECTOR right = XMVectorSet(1.f, 0.f, 0.f, 0.f);
        XMVECTOR right_rot_quat = XMQuaternionRotationAxis(right, XMConvertToRadians(inst->right_angle));

        XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
        XMVECTOR up_rot_quat = XMQuaternionRotationAxis(up, XMConvertToRadians(inst->up_angle));

        XMVECTOR forward = XMVectorSet(0.f, 0.f, 1.f, 0.f);
        XMVECTOR forward_rot_quat = XMQuaternionRotationAxis(forward, XMConvertToRadians(inst->forward_angle));

        XMVECTOR rot_quat = XMQuaternionMultiply(forward_rot_quat, XMQuaternionMultiply(right_rot_quat, up_rot_quat));
        XMMATRIX rot_mat = XMMatrixRotationQuaternion(rot_quat);

        XMMATRIX scale_mat = XMMatrixScaling(scale[0], scale[1], scale[2]);
        XMMATRIX translation_mat = XMMatrixTranslation(inst->translation[0], inst->translation[1], inst->translation[2]);
        XMMATRIX srt = scale_mat * rot_mat * translation_mat;

        if (num_selected_instances > 0)
        {
        }

        XMMATRIX new_world = XMMatrixIdentity();
        new_world = new_world * srt;

        XMStoreFloat4x4(&inst->shader_data.world, new_world);
        XMStoreFloat4x4(&inst_shader_data.world, XMMatrixTranspose(inst_world));

        frame->sb_instancedata_upload->copy_data((int)i, (void *)&inst_shader_data);
    }

    // render
    cmd_alloc->Reset();
    cmdlist->Reset(cmd_alloc, flat_color_pso);

    cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                    dr->main_rt_resources[backbuffer_index],
                                    D3D12_RESOURCE_STATE_PRESENT,
                                    D3D12_RESOURCE_STATE_RENDER_TARGET));

    set_viewport_rects(cmdlist);
    cmdlist->ClearDepthStencilView(dr->dsv_heap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.f, 0, 0, NULL);
    cmdlist->ClearRenderTargetView(dr->rtv_descriptors[backbuffer_index], bg_color, 0, NULL);
    cmdlist->OMSetRenderTargets(1, &dr->rtv_descriptors[backbuffer_index], FALSE, &dr->dsv_heap->GetCPUDescriptorHandleForHeapStart());

    switch (shading)
    {
    case flat_color:
        cmdlist->SetPipelineState(flat_color_pso);
        break;

    case wireframe:
        cmdlist->SetPipelineState(wireframe_pso);
        break;

    default:
        break;
    }

    cmdlist->SetDescriptorHeaps(1, &instanceIDs_srv_heap);
    cmdlist->SetGraphicsRootSignature(dr->rootsig);
    cmdlist->SetGraphicsRootConstantBufferView(0, frame->cb_passdata_upload->m_uploadbuffer->GetGPUVirtualAddress());
    draw_render_items(cmdlist, &render_items);

    query->start("ui_rendering");
    imgui_render(cmdlist);
    query->stop("ui_rendering");

    cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                    dr->main_rt_resources[backbuffer_index],
                                    D3D12_RESOURCE_STATE_RENDER_TARGET,
                                    D3D12_RESOURCE_STATE_PRESENT));

    query->resolve();
    cmdlist->Close();
    dr->cmd_queue->ExecuteCommandLists((UINT)1, (ID3D12CommandList *const *)&cmdlist);

    dr->present(is_vsync);
    frame->fence_value = dr->signal();

    imgui_gpu_time = query->result("ui_rendering");

    return true;
}

void pick(const DirectX::XMMATRIX &to_projection, const DirectX::XMMATRIX &to_view, const float ndc_x, const float ndc_y)
{
    float p00 = to_projection.r[0].m128_f32[0];
    float p11 = to_projection.r[1].m128_f32[1];

    float point_x = ndc_x / p00;
    float point_y = ndc_y / p11;

    XMMATRIX inv_view = XMMatrixInverse(&XMMatrixDeterminant(to_view), to_view);
    is_item_picked = false;

    for (render_item &ri : render_items)
    {
        for (instance &inst : ri.instances)
        {
            XMVECTOR ray_origin = XMVectorSet(0.f, 0.f, 0.f, 1.f);
            XMVECTOR ray_dir = XMVectorSet(point_x, point_y, 1.f, 0.f);

            XMMATRIX inst_world = XMLoadFloat4x4(&inst.shader_data.world);
            XMMATRIX inv_world = XMMatrixInverse(&XMMatrixDeterminant(inst_world), inst_world);
            XMMATRIX view_to_local = inv_view * inv_world;

            // Transform ray to local space of the current mesh we are testing against
            ray_origin = XMVector3TransformCoord(ray_origin, view_to_local);
            ray_dir = XMVector3Normalize(XMVector3TransformNormal(ray_dir, view_to_local));

            float dist = 0.f;
            if (inst.bounds.Intersects(ray_origin, ray_dir, dist))
            {
                if (!ImGui::GetIO().KeyCtrl)
                {
                    for (render_item &ri : render_items)
                    {
                        ri.is_selected = false;
                        for (instance &inst : ri.instances)
                        {
                            inst.is_selected = false;
                        }
                    }
                }
                inst.is_selected = true;
                ri.is_selected = true;
            }
        }
    }
}

extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam)
{
    imgui_wndproc(msg, wParam, lParam);

    switch (msg)
    {
    case WM_MOUSEMOVE:
        if (!is_hovering_window())
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                cam.update_yaw_pitch(XMFLOAT2(ImGui::GetMousePos().x, ImGui::GetMousePos().y),
                                     XMFLOAT2(last_mouse_pos.x, last_mouse_pos.y));
            }
        }
        last_mouse_pos.x = ImGui::GetMousePos().x;
        last_mouse_pos.y = ImGui::GetMousePos().y;
        break;

    case WM_LBUTTONDOWN:
        if (!is_hovering_window())
        {
            pick(XMMatrixTranspose(XMLoadFloat4x4(&pass.proj)), XMMatrixTranspose(XMLoadFloat4x4(&pass.view)), ndc_mouse_pos.x, ndc_mouse_pos.y);
        }
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
    safe_release(outline_pso);
    safe_release(stencil_pso);
    safe_release(cmdlist);
    safe_release(ui_requests_cmdlist);
    safe_release(ui_requests_cmdalloc);
    safe_release(instanceIDs_srv_heap);

    for (std::pair<std::wstring, mesh> geometry : geometries)
    {
        safe_release(geometry.second.resource->vertex_default);
        safe_release(geometry.second.resource->vertex_upload);
        safe_release(geometry.second.resource->index_default);
        safe_release(geometry.second.resource->index_upload);
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
        safe_release(debug);
    }
#endif
}
