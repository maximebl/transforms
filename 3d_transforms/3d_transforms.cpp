#pragma once
#include "pch.h"
#include <gpu_interface.h>
#include <gpu_query.h>
#include <imgui_helpers.h>
#include "frame_resource.h"
#include <vector>
#include <unordered_map>
#include <algorithm>
#include "shlwapi.h"
#include "math_helpers.h"
#include "cameraz.h"
camera cam;

using namespace DirectX;

extern "C" __declspec(dllexport) bool update_and_render();
extern "C" __declspec(dllexport) void resize(int width, int height);
extern "C" __declspec(dllexport) void cleanup();
extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) bool initialize();

internal void init_pipeline();
internal void create_render_items();
internal void draw_render_items(ID3D12GraphicsCommandList *cmd_list, const std::vector<render_item> *render_items);
internal void update_camera();

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
#define MAX_RENDER_ITEMS 10
#define NUM_RENDER_ITEMS 2
#define MAX_INSTANCE_COUNT_PER_OBJECT 1
internal std::vector<render_item> render_items;

// Geometry
internal std::unordered_map<std::wstring, mesh> geometries;

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
    create_render_items();

    cmdlist->Close();
    dr->cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&cmdlist);
    dr->flush_cmd_queue();
    return true;
}

internal void create_render_items()
{
    for (int i = 0; i < NUM_RENDER_ITEMS; i++)
    {
        render_item ri;
        ri.meshes = geometries[L"bunny"];
        ri.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        ri.cb_index = i;
        ri.id = i;
        ri.vertex_count = ri.meshes.vertex_count;
        ri.index_count = ri.meshes.index_count;
        ri.instance_count = MAX_INSTANCE_COUNT_PER_OBJECT;
        ri.name = "my cool cube";
        XMMATRIX world = XMMatrixIdentity();
        float amount = i * 3.f;
        XMMATRIX t = XMMatrixTranslation(amount, amount, amount);
        world = world * t;
        XMStoreFloat4x4(&ri.world, world);

        // create instance data
        for (int i = 0; i < MAX_INSTANCE_COUNT_PER_OBJECT; i++)
        {
            ri.instance_data.resize(MAX_INSTANCE_COUNT_PER_OBJECT);

            XMMATRIX world = XMMatrixIdentity();
            float amount = i * 3.f;
            XMMATRIX t = XMMatrixTranslation(amount, amount, amount);
            world = world * t;
            XMStoreFloat4x4(&ri.instance_data[i].world, world);
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
        cmd_list->IASetVertexBuffers(0, 1, &ri.meshes.resource->vbv);
        cmd_list->IASetIndexBuffer(&ri.meshes.resource->ibv);

        cmd_list->SetGraphicsRootShaderResourceView(2, frame->sb_instancedata_upload->m_uploadbuffer->GetGPUVirtualAddress());

        UINT cb_offset = ri.cb_index * cb_size;
        cmd_list->SetGraphicsRootConstantBufferView(1, cb_resource_address + cb_offset);
        //cmd_list->DrawInstanced(ri.vertex_count, ri.instance_count, ri.start_index_location, ri.base_vertex_location);
        cmd_list->DrawIndexedInstanced(ri.index_count, ri.instance_count, ri.start_index_location, ri.base_vertex_location, 0);
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

        render_item ri;
        ri.meshes = geometries[mesh_name];
        ri.topology = D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
        ri.cb_index = id;
        ri.id = id;
        ri.vertex_count = ri.meshes.vertex_count;
        ri.index_count = ri.meshes.index_count;
        ri.instance_count = MAX_INSTANCE_COUNT_PER_OBJECT;
        ri.name = _strdup(ri_name);
        XMStoreFloat4x4(&ri.world, XMMatrixIdentity());

        // create instance data
        for (int i = 0; i < MAX_INSTANCE_COUNT_PER_OBJECT; i++)
        {
            ri.instance_data.resize(MAX_INSTANCE_COUNT_PER_OBJECT);
            XMStoreFloat4x4(&ri.instance_data[i].world, XMMatrixIdentity());
        }
        render_items.push_back(ri);

        ui_requests_cmdlist->Close();
        dr->cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&ui_requests_cmdlist);
    }
    return 0;
}

XMVECTOR camera_dir = XMVectorSet(0.f, 0.f, 1.f, 0.f);
XMVECTOR camera_right = XMVectorSet(1.f, 0.f, 0.f, 0.f);
XMVECTOR camera_up = XMVectorSet(0.f, 1.f, 0.f, 0.f);
XMVECTOR camera_target = XMVectorSet(0.f, 0.f, 0.f, 1.f);
float angle = 0.f;
bool is_orbiting = false;
XMVECTOR orbit_target_pos = XMVectorSet(0.f, 0.f, 0.f, 1.f);

internal void update_camera()
{
    //x = radius * sinf(phi) * cosf(theta); // zero
    //y = radius * cosf(phi);               // 3.53
    //z = radius * sinf(phi) * sinf(theta); // -3.53
    //cam.position = XMVectorSet(x, y, z, 1.0f);
    //XMVECTOR camera_pos = XMVectorSet(x, y, z, 1.0f);
    //XMMATRIX view = XMMatrixTranspose(view_matrix_lh(cam.position, world_up_dir, target_pos));

    ImGui::SliderFloat4("Right direction", camera_right.m128_f32, -1.f, 1.f);
    ImGui::SliderFloat4("Up direction", camera_up.m128_f32, -1.f, 1.f);
    ImGui::SliderFloat4("Forward direction", camera_dir.m128_f32, -1.f, 1.f);
    ImGui::SliderFloat4("Camera position", cam.position.m128_f32, -150.f, 150.f);
    ImGui::SliderFloat("Camera roll", &cam.roll, -180.f, +180.f);
    ImGui::SliderFloat("Camera pitch", &cam.pitch, -180.f, +180.f);
    ImGui::SliderFloat("Camera yaw", &cam.yaw, -180.f, +180.f);
    ImGui::SliderFloat("Angle", &angle, 0, 180);
    ImGui::Checkbox("is orbiting", &is_orbiting);
    for (int i = 0; i < render_items.size(); i++)
    {
        render_item *ri = &render_items[i];
        if (ri->is_selected == true)
        {
            orbit_target_pos.m128_f32[0] = ri->world._41;
            orbit_target_pos.m128_f32[1] = ri->world._42;
            orbit_target_pos.m128_f32[2] = ri->world._43;
            orbit_target_pos.m128_f32[3] = ri->world._44;
            ImGui::SliderFloat4("Orbit target position", orbit_target_pos.m128_f32, -50.f, 50.f);
        }
    }

    camera_dir = XMVector3Normalize(camera_dir);

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
        cam.position = XMVectorMultiplyAdd(s, camera_dir, cam.position);
    }
    if (GetAsyncKeyState('S'))
    {
        cam.position = XMVectorMultiplyAdd(-s, camera_dir, cam.position);
    }
    if (GetAsyncKeyState('A'))
    {
        cam.position = XMVectorMultiplyAdd(-s, camera_right, cam.position);
    }
    if (GetAsyncKeyState('D'))
    {
        cam.position = XMVectorMultiplyAdd(s, camera_right, cam.position);
    }

    camera_dir = XMVector3Normalize(camera_dir);
    // The order of these 2 matters
    camera_right = XMVector3Cross(camera_up, camera_dir);
    camera_up = XMVector3Normalize(XMVector3Cross(camera_dir, camera_right));

    // Camera translation
    float x = -XMVectorGetX(XMVector3Dot(camera_right, cam.position));
    float y = -XMVectorGetX(XMVector3Dot(camera_up, cam.position));
    float z = -XMVectorGetX(XMVector3Dot(camera_dir, cam.position));

    XMMATRIX T;
    T.r[0] = camera_right;
    T.r[0].m128_f32[3] = x;

    T.r[1] = camera_up;
    T.r[1].m128_f32[3] = y;

    T.r[2] = camera_dir;
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

void imgui_update()
{
    imgui_new_frame();

    ImGui::Begin("3D Transforms", &show_main);
    imgui_app_combo();

    if (ImGui::Button("Load model"))
    {
        HANDLE ui_request_handle = CreateThread(nullptr, 0, pick_and_load_model, nullptr, 0, nullptr);
        CloseHandle(ui_request_handle);
    }

    if (ImGui::CollapsingHeader("Render items"))
    {
        bool node_expanded = false;
        bool inner_node_expanded = false;
        float indentation = 10.f;

        for (int i = 0; i < render_items.size(); i++)
        {
            ImGui::PushID(i);
            node_expanded = ImGui::TreeNodeExV("", ImGuiTreeNodeFlags_FramePadding, "", nullptr);
            ImGui::SameLine();
            ImGui::PopID();

            if (ImGui::Selectable(render_items[i].name.c_str(), render_items[i].is_selected))
            {
                if (!ImGui::GetIO().KeyCtrl) // Clear selection when CTRL is not held
                {
                    for (size_t k = 0; k < render_items.size(); k++)
                    {
                        render_items[k].is_selected = false;
                        for (size_t l = 0; l < render_items[k].meshes.submeshes.size(); l++)
                        {
                            render_items[k].meshes.submeshes[l].is_selected = false;
                        }
                    }
                }

                for (size_t k = 0; k < render_items[i].meshes.submeshes.size(); k++)
                {
                    render_items[i].meshes.submeshes[k].is_selected = true;
                }
                render_items[i].is_selected = true;
            }

            if (node_expanded)
            {
                float ri_pos[4];
                ri_pos[0] = render_items[i].world._41;
                ri_pos[1] = render_items[i].world._42;
                ri_pos[2] = render_items[i].world._43;
                ri_pos[3] = render_items[i].world._44;
                ImGui::Indent(indentation);
                ImGui::Text("ID: %d", render_items[i].id);
                ImGui::Text("Instance count: %d", render_items[i].instance_count);
                ImGui::Text("Vertices total: %d", render_items[i].vertex_count);
                ImGui::Text("Indices total: %d", render_items[i].index_count);
                ImGui::InputFloat4("Position ", ri_pos, 3);
                ImGui::NewLine();
                ImGui::Text("Submeshes:");
                ImGui::Indent(indentation);

                for (int j = 0; j < render_items[i].meshes.submeshes.size(); j++)
                {
                    ImGui::PushID(j);
                    inner_node_expanded = ImGui::TreeNodeExV("", ImGuiTreeNodeFlags_FramePadding, "", nullptr);
                    ImGui::SameLine();

                    if (ImGui::Selectable(render_items[i].meshes.submeshes[j].name.c_str(), render_items[i].meshes.submeshes[j].is_selected))
                    {
                        if (!ImGui::GetIO().KeyCtrl) // Clear selection when CTRL is not held
                        {
                            for (size_t k = 0; k < render_items.size(); k++)
                            {
                                render_items[k].is_selected = false;
                                for (size_t l = 0; l < render_items[k].meshes.submeshes.size(); l++)
                                {
                                    render_items[k].meshes.submeshes[l].is_selected = false;
                                }
                            }
                        }
                        render_items[i].meshes.submeshes[j].is_selected = true;
                    }

                    ImGui::PopID();
                    if (inner_node_expanded)
                    {
                        ImGui::Indent(indentation);
                        ImGui::Text("Vertices: %d", render_items[i].meshes.submeshes[j].vertex_count);
                        ImGui::Text("Indices: %d", render_items[i].meshes.submeshes[j].index_count);
                        ImGui::Unindent(indentation);
                        ImGui::TreePop();
                    }
                }
                ImGui::Unindent(indentation);
                ImGui::Unindent(indentation);
                ImGui::TreePop();
            }
        }
    }

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

    // update object constant data
    for (int i = 0; i < render_items.size(); i++)
    {
        render_item ri = render_items[i];
        XMMATRIX world = XMLoadFloat4x4(&ri.world);
        XMFLOAT4X4 transposed;
        XMStoreFloat4x4(&transposed, XMMatrixTranspose(world));

        frame->cb_objconstant_upload->copy_data(ri.cb_index, (void *)&transposed);

        // update instance data
        for (int j = 0; j < ri.instance_data.size(); j++)
        {
            instance_data instance = ri.instance_data[j];
            world = XMLoadFloat4x4(&instance.world);
            XMStoreFloat4x4(&transposed, XMMatrixTranspose(world));

            frame->sb_instancedata_upload->copy_data(j, (void *)&transposed);
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

                float dx = XMConvertToRadians(3.5f * (x - last_mouse_pos.x));
                float dy = XMConvertToRadians(3.5f * (y - last_mouse_pos.y));

                camera_dir = XMVector3Normalize(camera_dir);

                if (GetAsyncKeyState(VK_MENU))
                {
                    if (is_orbiting == false)
                    {
                        // first time entering orbit mode
                    }

                    is_orbiting = true;

                    // translate cam position to origin
                    XMVECTOR start_cam_pos = cam.position;
                    XMMATRIX to_origin = XMMatrixTranslationFromVector(-cam.position);
                    cam.position = XMVector3Transform(cam.position, to_origin);

                    // rotate the camera direction
                    XMMATRIX to_yaw = XMMatrixRotationY(XMConvertToRadians(dx));
                    XMVECTOR rot_cam_dir = XMVector3Normalize(XMVector3Transform(camera_dir, to_yaw));
                    // rotate the right vector to reorient the viewer
                    camera_right = XMVector3TransformNormal(camera_right, to_yaw);

                    XMMATRIX to_pitch = XMMatrixRotationAxis(camera_right, XMConvertToRadians(dy));
                    rot_cam_dir = XMVector3TransformNormal(rot_cam_dir, to_pitch);
                    camera_up = XMVector3TransformNormal(camera_up, to_pitch);

                    // get the distance from the camera position to the object it orbits
                    XMVECTOR cam_to_target = XMVectorSubtract(orbit_target_pos, start_cam_pos);
                    float dist_to_target = XMVectorGetX(XMVector3Length(cam_to_target));

                    // Place the camera back
                    rot_cam_dir = XMVectorScale(rot_cam_dir, -dist_to_target);
                    cam.position = XMVectorAdd(cam.position, rot_cam_dir);

                    //look at the thing we're orbiting around
                    camera_dir = XMVector4Normalize(XMVectorSubtract(orbit_target_pos, cam.position));
                }
                else
                {
                    XMMATRIX yaw = XMMatrixRotationY(XMConvertToRadians(dx));
                    camera_dir = XMVector3TransformNormal(camera_dir, yaw);
                    camera_right = XMVector3TransformNormal(camera_right, yaw);
                    camera_up = XMVector3TransformNormal(camera_up, yaw);

                    XMMATRIX pitch = XMMatrixRotationAxis(camera_right, XMConvertToRadians(dy));
                    camera_dir = XMVector3TransformNormal(camera_dir, pitch);
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

#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1 *debug = NULL;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
    {
        debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
        debug->Release();
    }
#endif
}
