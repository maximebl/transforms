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
#include "cameraz.h"
#include <functional>
#include <tuple>
camera cam;

using namespace DirectX;

extern "C" __declspec(dllexport) bool update_and_render();
extern "C" __declspec(dllexport) void resize(int width, int height);
extern "C" __declspec(dllexport) void cleanup();
extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) bool initialize();

internal void init_pipeline();
internal void create_render_item(std::string name, std::wstring mesh_name, int id);
internal void draw_render_items(ID3D12GraphicsCommandList *cmd_list, const std::vector<render_item> *render_items);
internal void update_camera();
internal void update_orbit(float x, float y);

// command objects
internal device_resources *dr = nullptr;
internal ID3D12Device *device = nullptr;
internal ID3D12GraphicsCommandList *cmdlist = nullptr;
internal ID3D12GraphicsCommandList *ui_requests_cmdlist = nullptr;
internal ID3D12CommandAllocator *ui_requests_cmdalloc = nullptr;
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
internal ID3D12PipelineState *line_pso = nullptr;
internal ID3D12PipelineState *wireframe_pso = nullptr;

// Shader data
internal ID3DBlob *debugfx_blob_vs = nullptr;
internal ID3DBlob *debugfx_blob_ps = nullptr;
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
internal bool show_selection = true;

// Render items data
#define MAX_RENDER_ITEMS 10
#define NUM_RENDER_ITEMS 1
#define MAX_INSTANCE_COUNT_PER_OBJECT 2
internal std::vector<render_item> render_items;

// Geometry
internal std::unordered_map<std::wstring, mesh> geometries;

// Debug effects
#define MAX_DEBUG_LINES 1000
upload_buffer *debug_lines_upload = nullptr;
D3D12_VERTEX_BUFFER_VIEW debug_lines_vbv = {};
struct debug_line
{
    position_color start;
    position_color end;
};
internal debug_line orbit_line;
internal std::vector<debug_line *> debug_lines;
internal void draw_debug_lines(ID3D12GraphicsCommandList *cmd_list, const std::vector<debug_line *> *debug_lines);
bool is_line_buffer_ready = false;

// Camera
internal pass_data pass;
internal float radius = 5.0f;
internal ImVec2 last_mouse_pos;
internal float dx_angle = 0.f;
internal float dy_angle = 0.f;

XMVECTOR camera_forward = XMVectorSet(0.f, 0.f, 1.f, 0.f);
XMVECTOR camera_right = XMVectorSet(1.f, 0.f, 0.f, 0.f);
XMVECTOR camera_up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
bool is_orbiting = false;
XMVECTOR orbit_target_pos = XMVectorSet(0.f, 0.f, 0.f, 1.f);

// Selection
internal instance *current_instance;
internal float translation[3];
internal float scale[3];
internal float right_angle = 0.f;
internal float up_angle = 0.f;
internal float forward_angle = 0.f;
internal std ::vector<render_item> selected_render_items;

extern "C" __declspec(dllexport) bool initialize()
{
    if (!XMVerifyCPUSupport())
        return false;

    dr = new device_resources();
    device = dr->device;

    imgui_init(dr->device);

    if (!compile_shader(L"..\\..\\3d_transforms\\shaders\\debug_fx.hlsl", L"VS", shader_type::vertex, &debugfx_blob_vs))
        return false;
    if (!compile_shader(L"..\\..\\3d_transforms\\shaders\\debug_fx.hlsl", L"PS", shader_type::pixel, &debugfx_blob_ps))
        return false;
    if (!compile_shader(L"..\\..\\3d_transforms\\shaders\\perspective.hlsl", L"VS", shader_type::vertex, &perspective_blob_vs))
        return false;
    if (!compile_shader(L"..\\..\\3d_transforms\\shaders\\perspective.hlsl", L"PS", shader_type::pixel, &perspective_blob_ps))
        return false;

    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
        frame_resources[i] = new frame_resource(device, i,
                                                MAX_RENDER_ITEMS,
                                                MAX_RENDER_ITEMS * MAX_INSTANCE_COUNT_PER_OBJECT,
                                                1);
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

    default_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    default_pso_desc.VS = {debugfx_blob_vs->GetBufferPointer(), debugfx_blob_vs->GetBufferSize()};
    hr = device->CreateGraphicsPipelineState(&default_pso_desc, IID_PPV_ARGS(&line_pso));
    ASSERT(SUCCEEDED(hr));

    D3D12_GRAPHICS_PIPELINE_STATE_DESC wireframe_pso_desc = dr->create_default_pso(&input_elem_descs, perspective_blob_vs, perspective_blob_ps);
    wireframe_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    wireframe_pso_desc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
    hr = device->CreateGraphicsPipelineState(&wireframe_pso_desc, IID_PPV_ARGS(&wireframe_pso));
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

    mesh_name = L"bunny";
    mesh bunny;
    create_mesh_data(device, cmdlist, mesh_name,
                     sizeof(position_color), bunny_vertices.size(), (void *)bunny_vertices.data(),
                     sizeof(WORD), bunny_indices.size(), (void *)bunny_indices.data(),
                     &bunny);
    geometries[mesh_name] = bunny;

    for (int i = 0; i < NUM_RENDER_ITEMS; i++)
    {
        create_render_item("my cool bunny", mesh_name, i);
    }

    cmdlist->Close();
    dr->cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&cmdlist);
    dr->flush_cmd_queue();
    return true;
}

internal void create_render_item(std::string name, std::wstring mesh_name, int id)
{
    render_item ri;
    ri.meshes = geometries[mesh_name];
    ri.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
    ri.cb_index = id;
    ri.id = id;
    ri.vertex_count = ri.meshes.vertex_count;
    ri.index_count = ri.meshes.index_count;
    ri.name = name;
    XMMATRIX world = XMMatrixIdentity();
    float amount = id * 3.f;
    XMMATRIX t = XMMatrixTranslation(amount, amount, amount);
    world = world * t;
    XMStoreFloat4x4(&ri.world, world);

    // create instance data
    for (int i = 0; i < MAX_INSTANCE_COUNT_PER_OBJECT; i++)
    {
        instance inst;
        inst.name = "instance_" + std::to_string(i);
        XMMATRIX world = XMMatrixIdentity();

        XMVECTOR right = XMVectorSet(1.f, 0.f, 0.f, 0.f);
        XMVECTOR right_rot_quat = XMQuaternionRotationAxis(right, XMConvertToRadians(right_angle));

        XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
        XMVECTOR up_rot_quat = XMQuaternionRotationAxis(up, XMConvertToRadians(up_angle));

        XMVECTOR forward = XMVectorSet(0.f, 0.f, 1.f, 0.f);
        XMVECTOR forward_rot_quat = XMQuaternionRotationAxis(forward, XMConvertToRadians(forward_angle));

        XMVECTOR rot_quat = XMQuaternionMultiply(forward_rot_quat, XMQuaternionMultiply(right_rot_quat, up_rot_quat));
        XMMATRIX rot_mat = XMMatrixRotationQuaternion(rot_quat);

        inst.scale[0] = 1.f;
        inst.scale[1] = 1.f;
        inst.scale[2] = 1.f;
        XMMATRIX scale_mat = XMMatrixScaling(inst.scale[0], inst.scale[1], inst.scale[2]);

        inst.translation[0] = i * 3.f;
        inst.translation[1] = i * 3.f;
        inst.translation[2] = i * 3.f;
        XMMATRIX translation_mat = XMMatrixTranslation(inst.translation[0], inst.translation[1], inst.translation[2]);
        XMMATRIX srt = scale_mat * rot_mat * translation_mat;
        world = world * srt;

        XMStoreFloat4x4(&inst.shader_data.world, world);

        ri.instances.push_back(inst);
    }
    render_items.push_back(ri);
}

internal void draw_debug_lines(ID3D12GraphicsCommandList *cmd_list, std::vector<debug_line *> *debug_lines)
{
    if (!is_line_buffer_ready)
    {
        debug_lines->push_back(&orbit_line);
        size_t line_stride = sizeof(position_color);
        size_t lines_byte_size = line_stride * MAX_DEBUG_LINES;
        debug_lines_upload = new upload_buffer(device, 1, lines_byte_size, "debug_lines");

        debug_lines_vbv = {};
        debug_lines_vbv.BufferLocation = debug_lines_upload->m_uploadbuffer->GetGPUVirtualAddress();
        debug_lines_vbv.SizeInBytes = (UINT)lines_byte_size;
        debug_lines_vbv.StrideInBytes = (UINT)line_stride;
        is_line_buffer_ready = true;
    }

    if (debug_lines->size() > 0)
    {
        debug_lines_upload->copy_data(0, (void *)*debug_lines->data());

        cmdlist->SetPipelineState(line_pso);
        cmdlist->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        cmdlist->IASetVertexBuffers(0, 1, &debug_lines_vbv);
        cmdlist->DrawInstanced(2, 1, 0, 0);
    }
}

internal void draw_render_items(ID3D12GraphicsCommandList *cmd_list, const std::vector<render_item> *render_items)
{
    size_t cb_size = frame->cb_objconstants_size;
    size_t cb_resource_address = frame->cb_objconstant_upload->m_uploadbuffer->GetGPUVirtualAddress();

    for (render_item ri : *render_items)
    {
        cmd_list->IASetPrimitiveTopology(ri.topology);
        cmd_list->IASetVertexBuffers(0, 1, &ri.meshes.resource->vbv);
        cmd_list->IASetIndexBuffer(&ri.meshes.resource->ibv);

        cmd_list->SetGraphicsRootShaderResourceView(2, frame->sb_instancedata_upload->m_uploadbuffer->GetGPUVirtualAddress());

        size_t cb_offset = ri.cb_index * cb_size;
        cmd_list->SetGraphicsRootConstantBufferView(1, cb_resource_address + cb_offset);
        cmd_list->DrawIndexedInstanced(ri.index_count, (UINT)ri.instances.size(), ri.start_index_location, ri.base_vertex_location, 0);
    }
}

DWORD __stdcall pick_and_load_model(void *param)
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

        for (int i = 0; i < meshdata.size(); i++)
        {
            submesh new_submesh;
            new_submesh.name = meshdata[i].name;
            new_submesh.vertex_count = (UINT)meshdata[i].vertices.size();
            new_submesh.index_count = (UINT)meshdata[i].indices.size();
            submeshes.push_back(new_submesh);

            for (int j = 0; j < meshdata[i].vertices.size(); j++)
            {
                mesh_vertices.push_back(meshdata[i].vertices[j]);
            }

            for (int j = 0; j < meshdata[i].indices.size(); j++)
            {
                mesh_indices.push_back(meshdata[i].indices[j]);
            }
        }

        PathRemoveExtensionW(ofn.lpstrFile);
        const wchar_t *mesh_name = PathFindFileNameW(ofn.lpstrFile);
        mesh new_mesh;
        new_mesh.submeshes = submeshes;

        create_mesh_data(device, ui_requests_cmdlist, mesh_name,
                         sizeof(position_color), mesh_vertices.size(), (void *)mesh_vertices.data(),
                         sizeof(WORD), mesh_indices.size(), (void *)mesh_indices.data(),
                         &new_mesh);

        geometries[mesh_name] = new_mesh;


        UINT id = (UINT)render_items.size();
        char id_str[12];
        sprintf(id_str, "_%u", id);
        char ri_name[MAX_PATH];
        wcstombs(ri_name, mesh_name, 100);
        strcat(ri_name, id_str);

        create_render_item(ri_name, mesh_name, id);

        //render_item ri;
        //ri.meshes = geometries[mesh_name];
        //ri.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        //ri.cb_index = id;
        //ri.id = id;
        //ri.vertex_count = ri.meshes.vertex_count;
        //ri.index_count = ri.meshes.index_count;
        //ri.name = _strdup(ri_name);
        //XMStoreFloat4x4(&ri.world, XMMatrixIdentity());

        //// create instance data
        //ri.instances.resize(MAX_INSTANCE_COUNT_PER_OBJECT);

        //for (int i = 0; i < MAX_INSTANCE_COUNT_PER_OBJECT; i++)
        //{
        //    instance *inst = &ri.instances[i];
        //    inst->name = "instance_" + std::to_string(i);
        //    XMVECTOR right = XMVectorSet(1.f, 0.f, 0.f, 0.f);
        //    XMVECTOR right_rot_quat = XMQuaternionRotationAxis(right, XMConvertToRadians(right_angle));

        //    XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
        //    XMVECTOR up_rot_quat = XMQuaternionRotationAxis(up, XMConvertToRadians(up_angle));

        //    XMVECTOR forward = XMVectorSet(0.f, 0.f, 1.f, 0.f);
        //    XMVECTOR forward_rot_quat = XMQuaternionRotationAxis(forward, XMConvertToRadians(forward_angle));

        //    XMVECTOR rot_quat = XMQuaternionMultiply(forward_rot_quat, XMQuaternionMultiply(right_rot_quat, up_rot_quat));
        //    XMMATRIX rot_mat = XMMatrixRotationQuaternion(rot_quat);

        //    XMMATRIX scale_mat = XMMatrixScaling(1.f, 1.f, 1.f);
        //    inst->scale[0] = 1.f;
        //    inst->scale[1] = 1.f;
        //    inst->scale[2] = 1.f;

        //    float start_offset = i * 3.f;
        //    XMMATRIX translation_mat = XMMatrixTranslation(start_offset, start_offset, start_offset);
        //    inst->translation[0] = start_offset;
        //    inst->translation[1] = start_offset;
        //    inst->translation[2] = start_offset;

        //    XMMATRIX srt = translation_mat * rot_mat * scale_mat;
        //    XMMATRIX world = XMMatrixIdentity();
        //    world = world * srt;

        //    XMStoreFloat4x4(&inst->shader_data.world, world);
        //}
        //render_items.push_back(ri);

        ui_requests_cmdlist->Close();
        dr->cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&ui_requests_cmdlist);
    }
    return 0;
}

internal void update_camera()
{
    ImGui::SliderFloat4("Right direction", camera_right.m128_f32, -1.f, 1.f);
    ImGui::SameLine();
    if (ImGui::Button("Reset right direction"))
        camera_right = XMVectorSet(1.f, 0.f, 0.f, 0.f);

    ImGui::SliderFloat4("Up direction", camera_up.m128_f32, -1.f, 1.f);
    ImGui::SameLine();
    if (ImGui::Button("Reset up direction"))
        camera_up = XMVectorSet(0.f, 1.f, 0.f, 0.f);

    ImGui::SliderFloat4("Forward direction", camera_forward.m128_f32, -1.f, 1.f);
    ImGui::SameLine();
    if (ImGui::Button("Reset forward direction"))
        camera_forward = XMVectorSet(0.f, 0.f, 1.f, 0.f);

    ImGui::SliderFloat4("Camera position", cam.position.m128_f32, -150.f, 150.f);
    ImGui::SameLine();
    if (ImGui::Button("Reset camera position"))
    {
        cam.position = XMVectorZero();
    }

    ImGui::SliderFloat("Camera roll", &cam.roll, -180.f, +180.f);
    ImGui::SliderFloat("Camera pitch", &cam.pitch, -180.f, +180.f);
    ImGui::SliderFloat("Camera yaw", &cam.yaw, -180.f, +180.f);
    ImGui::Checkbox("is orbiting", &is_orbiting);

    camera_forward = XMVector3Normalize(camera_forward);
    // scale camera_dir by 15 units
    XMVECTOR scaled_cam_dir = XMVectorScale(camera_forward, 15.f);
    XMVECTOR default_orbit_target = cam.position + scaled_cam_dir;
    orbit_target_pos = default_orbit_target;

    for (render_item &ri : render_items)
        for (instance &inst : ri.instances)
            if (inst.is_selected == true)
            {
                orbit_target_pos.m128_f32[0] = inst.shader_data.world._41;
                orbit_target_pos.m128_f32[1] = inst.shader_data.world._42;
                orbit_target_pos.m128_f32[2] = inst.shader_data.world._43;
                orbit_target_pos.m128_f32[3] = inst.shader_data.world._44;
                ImGui::SliderFloat4("Orbit target position", orbit_target_pos.m128_f32, -50.f, 50.f);
            }

    XMVECTOR s = XMVectorReplicate(0.02f);
    if (GetAsyncKeyState('E'))
    {
        cam.position = XMVectorMultiplyAdd(-s, camera_up, cam.position);
    }

    if (GetAsyncKeyState('Q'))
    {
        cam.position = XMVectorMultiplyAdd(s, camera_up, cam.position);
    }

    if (GetAsyncKeyState('W'))
    {
        cam.position = XMVectorMultiplyAdd(s, camera_forward, cam.position);
    }
    if (GetAsyncKeyState('S'))
    {
        cam.position = XMVectorMultiplyAdd(-s, camera_forward, cam.position);
    }
    if (GetAsyncKeyState('A'))
    {
        cam.position = XMVectorMultiplyAdd(-s, camera_right, cam.position);
    }
    if (GetAsyncKeyState('D'))
    {
        cam.position = XMVectorMultiplyAdd(s, camera_right, cam.position);
    }

    camera_up = XMVector3Normalize(camera_up);
    camera_forward = XMVector3Normalize(camera_forward);
    camera_right = XMVector3Cross(camera_up, camera_forward);
    camera_up = XMVector3Normalize(XMVector3Cross(camera_forward, camera_right));

    // Camera translation
    float x = -XMVectorGetX(XMVector3Dot(camera_right, cam.position));
    float y = -XMVectorGetX(XMVector3Dot(camera_up, cam.position));
    float z = -XMVectorGetX(XMVector3Dot(camera_forward, cam.position));

    XMMATRIX T;
    T.r[0] = camera_right;
    T.r[0].m128_f32[3] = x;

    T.r[1] = camera_up;
    T.r[1].m128_f32[3] = y;

    T.r[2] = camera_forward;
    T.r[2].m128_f32[3] = z;

    T.r[3] = XMVectorSet(0.f, 0.f, 0.f, 1.f);

    XMMATRIX view = T;
    XMStoreFloat4x4(&pass.view, view);

    // projection matrix
    float aspect_ratio = (float)g_hwnd_width / (float)g_hwnd_height;
    XMMATRIX proj = XMMatrixTranspose(XMMatrixPerspectiveFovLH(0.25f * XM_PI, aspect_ratio, 1.0f, 1000.0f));
    XMStoreFloat4x4(&pass.proj, proj);

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

            tree_ri.on_select_callback = [](tree_item *ti) {
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
        for (tree_item &item : selected_items)
        {
            *item.is_selected = false;
            selected_items.clear();
        }

    imgui_new_frame();

    ImGui::Begin("3D Transforms", &show_main);
    imgui_app_combo();

    if (ImGui::Button("Load model"))
    {
        HANDLE ui_request_handle = CreateThread(nullptr, 0, pick_and_load_model, nullptr, 0, nullptr);
        CloseHandle(ui_request_handle);
    }

    imgui_nested_tree();
    ImGui::End();

    ImGui::Begin("Selection", &show_selection);
    if (current_instance)
    {
        ImGui::SliderFloat3("Translation", current_instance->translation, -20.f, 20.f);
        ImGui::SameLine();
        if (ImGui::Button("Reset translation"))
        {
            current_instance->translation[0] = 0.f;
            current_instance->translation[1] = 0.f;
            current_instance->translation[2] = 0.f;
        }
        ImGui::SliderFloat("Right angle", &current_instance->right_angle, 0, 360);
        ImGui::SameLine();
        if (ImGui::Button("Reset right angle"))
        {
            current_instance->right_angle = 0.f;
        }
        ImGui::SliderFloat("Up angle", &current_instance->up_angle, 0, 360);
        ImGui::SameLine();
        if (ImGui::Button("Reset up angle"))
        {
            current_instance->up_angle = 0.f;
        }
        ImGui::SliderFloat("Forward angle", &current_instance->forward_angle, 0, 360);
        ImGui::SameLine();
        if (ImGui::Button("Reset forward angle"))
        {
            current_instance->forward_angle = 0.f;
        }

        ImGui::SliderFloat3("Scale", current_instance->scale, 01.f, 20.f);
        ImGui::SameLine();
        if (ImGui::Button("Reset scale"))
        {
            current_instance->scale[0] = 1.f;
            current_instance->scale[1] = 1.f;
            current_instance->scale[2] = 1.f;
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

    std::vector<instance> total_ri_instances;

    // update object constant data
    for (render_item &ri : render_items)
    {
        XMMATRIX world = XMLoadFloat4x4(&ri.world);
        XMFLOAT4X4 transposed;
        XMStoreFloat4x4(&transposed, XMMatrixTranspose(world));

        frame->cb_objconstant_upload->copy_data(ri.cb_index, (void *)&transposed);

        // update instance data
        for (int j = 0; j < ri.instances.size(); j++)
        {
            instance inst = ri.instances[j];
            XMMATRIX inst_world = XMLoadFloat4x4(&inst.shader_data.world);
            instance_data inst_shader_data;
            XMStoreFloat4x4(&inst_shader_data.world, XMMatrixTranspose(inst_world));

            if (inst.is_selected)
            {
                current_instance = &ri.instances[j];
                XMVECTOR right = XMVectorSet(1.f, 0.f, 0.f, 0.f);
                XMVECTOR right_rot_quat = XMQuaternionRotationAxis(right, XMConvertToRadians(current_instance->right_angle));

                XMVECTOR up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
                XMVECTOR up_rot_quat = XMQuaternionRotationAxis(up, XMConvertToRadians(current_instance->up_angle));

                XMVECTOR forward = XMVectorSet(0.f, 0.f, 1.f, 0.f);
                XMVECTOR forward_rot_quat = XMQuaternionRotationAxis(forward, XMConvertToRadians(current_instance->forward_angle));

                XMVECTOR rot_quat = XMQuaternionMultiply(forward_rot_quat, XMQuaternionMultiply(right_rot_quat, up_rot_quat));
                XMMATRIX rot_mat = XMMatrixRotationQuaternion(rot_quat);

                XMMATRIX scale_mat = XMMatrixScaling(current_instance->scale[0], current_instance->scale[1], current_instance->scale[2]);
                XMMATRIX translation_mat = XMMatrixTranslation(current_instance->translation[0], current_instance->translation[1], current_instance->translation[2]);
                XMMATRIX srt = scale_mat * rot_mat * translation_mat;

                XMMATRIX new_world = XMMatrixIdentity();
                new_world = new_world * srt;

                XMStoreFloat4x4(&current_instance->shader_data.world, new_world);
                XMStoreFloat4x4(&inst_shader_data.world, XMMatrixTranspose(inst_world));
            }
            frame->sb_instancedata_upload->copy_data(j, (void *)&inst_shader_data);
        }
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

    cmdlist->SetGraphicsRootSignature(dr->rootsig);
    cmdlist->SetGraphicsRootConstantBufferView(0, frame->cb_passdata_upload->m_uploadbuffer->GetGPUVirtualAddress());
    draw_render_items(cmdlist, &render_items);
    draw_debug_lines(cmdlist, &debug_lines);

    query->start_query("ui_rendering");
    imgui_render(cmdlist);
    query->end_query("ui_rendering");

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

void update_orbit(float x, float y)
{
    XMVECTOR start_cam_pos = cam.position;

    XMStoreFloat3(&orbit_line.start.position, start_cam_pos);
    XMStoreFloat3(&orbit_line.end.position, orbit_target_pos);
    XMStoreFloat4(&orbit_line.start.color, DirectX::Colors::Red.v);
    XMStoreFloat4(&orbit_line.end.color, DirectX::Colors::GreenYellow.v);

    // Get the direction vector from the camera position to the target position (position - point = direction)
    XMVECTOR cam_to_target_dir = orbit_target_pos - cam.position;

    // Translate to target position (direction + point = point)
    cam.position = cam_to_target_dir + cam.position;

    // Rotate the camera forward direction around the camera up vector.
    camera_up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
    XMVECTOR up_rot_quat = XMQuaternionRotationAxis(camera_up, x);
    XMMATRIX up_rot_mat = XMMatrixRotationQuaternion(up_rot_quat);
    XMVECTOR rot_cam_dir = XMVector3Normalize(XMVector3Transform(camera_forward, up_rot_mat));

    // Rotate the right vector to reorient the viewer
    camera_right = XMVector3TransformNormal(camera_right, up_rot_mat);

    // Rotate the camera forward direction around the camera right vector.
    XMVECTOR right_rot_quat = XMQuaternionRotationAxis(camera_right, y);
    XMMATRIX right_rot_mat = XMMatrixRotationQuaternion(right_rot_quat);
    rot_cam_dir = XMVector3TransformNormal(rot_cam_dir, right_rot_mat);
    camera_up = XMVector3TransformNormal(camera_up, right_rot_mat);

    // get the distance from the camera position to the object it orbits
    XMVECTOR cam_to_target = XMVectorSubtract(orbit_target_pos, start_cam_pos);
    float dist_to_target = XMVectorGetX(XMVector3Length(cam_to_target));

    // Place the camera back
    rot_cam_dir = XMVectorScale(rot_cam_dir, -dist_to_target);
    cam.position = XMVectorAdd(cam.position, rot_cam_dir);

    //look at the thing we're orbiting around
    camera_forward = XMVector4Normalize(XMVectorSubtract(orbit_target_pos, cam.position));
}

extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam)
{
    imgui_wndproc(msg, wParam, lParam);

    if (!GetAsyncKeyState(VK_MENU))
    {
        is_orbiting = false;
    }

    switch (msg)
    {
    case WM_MOUSEMOVE:
        if (!ImGui::IsAnyWindowHovered() && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemFocused() && !ImGui::IsAnyWindowFocused())
        {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                float x = ImGui::GetMousePos().x;
                float y = ImGui::GetMousePos().y;

                float dx = XMConvertToRadians(0.5f * (x - last_mouse_pos.x));
                float dy = XMConvertToRadians(0.5f * (y - last_mouse_pos.y));

                if (GetAsyncKeyState(VK_MENU))
                {
                    if (is_orbiting == false)
                    {
                        // first time entering orbit mode
                    }

                    is_orbiting = true;

                    //update_orbit(dx, dy);

                    XMVECTOR start_cam_pos = cam.position;

                    XMStoreFloat3(&orbit_line.start.position, start_cam_pos);
                    XMStoreFloat3(&orbit_line.end.position, orbit_target_pos);
                    XMStoreFloat4(&orbit_line.start.color, DirectX::Colors::Red.v);
                    XMStoreFloat4(&orbit_line.end.color, DirectX::Colors::GreenYellow.v);

                    // Get the direction vector from the camera position to the target position (position - point = direction)
                    XMVECTOR cam_to_target_dir = orbit_target_pos - cam.position;

                    // Translate to target position (direction + point = point)
                    cam.position = cam_to_target_dir + cam.position;

                    XMMATRIX to_yaw = XMMatrixRotationY(dx);
                    XMMATRIX to_pitch = XMMatrixRotationAxis(camera_right, dy);
                    XMMATRIX to_final = to_yaw * to_pitch;

                    XMVECTOR rot_cam_forward = XMVector3Normalize(XMVector3Transform(camera_forward, to_final));

                    // get the distance from the camera position to the object it orbits
                    XMVECTOR cam_to_target = XMVectorSubtract(orbit_target_pos, start_cam_pos);
                    float dist_to_target = XMVectorGetX(XMVector3Length(cam_to_target));

                    // Place the camera back
                    rot_cam_forward = XMVectorScale(rot_cam_forward, -dist_to_target);
                    cam.position = XMVectorAdd(cam.position, rot_cam_forward);

                    //look at the thing we're orbiting around
                    camera_forward = XMVector4Normalize(XMVectorSubtract(orbit_target_pos, cam.position));
                }
                else
                {
                    XMMATRIX yaw = XMMatrixRotationY(dx);
                    camera_forward = XMVector3TransformNormal(camera_forward, yaw);
                    camera_right = XMVector3TransformNormal(camera_right, yaw);
                    camera_up = XMVector3TransformNormal(camera_up, yaw);

                    XMMATRIX pitch = XMMatrixRotationAxis(camera_right, dy);
                    camera_forward = XMVector3TransformNormal(camera_forward, pitch);
                    camera_up = XMVector3TransformNormal(camera_up, pitch);
                }
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
    safe_release(line_pso);
    safe_release(wireframe_pso);
    safe_release(cmdlist);
    safe_release(ui_requests_cmdlist);
    safe_release(ui_requests_cmdalloc);

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
    delete debug_lines_upload;

#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1 *debug = NULL;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
    {
        debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
        debug->Release();
    }
#endif
}
