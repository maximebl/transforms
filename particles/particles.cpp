#include "pch.h"
#include <gpu_interface.h>
#include <imgui_helpers.h>
#include "frame_resource.h"
#include "gpu_query.h"
#include "step_timer.h"
#include "mcallister_system.h"
#include "camera.h"

using namespace DirectX;

extern "C" __declspec(dllexport) bool update_and_render();
extern "C" __declspec(dllexport) void resize(int width, int height);
extern "C" __declspec(dllexport) void cleanup();
extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) bool initialize();

// App state
s_internal void imgui_update();
s_internal bool show_demo = true;
s_internal bool is_vsync = true;
s_internal bool is_waiting_present = true;
s_internal cpu_timer timer;

// Command objects
s_internal device_resources *dr = nullptr;
s_internal ID3D12Device *device = nullptr;
s_internal frame_resource *frame_resources[NUM_BACK_BUFFERS] = {};
s_internal frame_resource *frame = nullptr;
s_internal ID3D12GraphicsCommandList *main_cmdlist = nullptr;
s_internal ID3D12CommandQueue *cmd_queue = nullptr;
s_internal ID3D12CommandAllocator *cmd_alloc = nullptr;

// Shader objects
bool create_shader_objects();
s_internal ID3DBlob *particle_blob_vs = nullptr;
s_internal ID3DBlob *particle_blob_ps = nullptr;
s_internal ID3DBlob *particle_blob_gs = nullptr;
s_internal ID3D12PipelineState *flat_color_pso = nullptr;

// Queries data
s_internal constexpr int NUM_QUERIES = 5;
s_internal gpu_query *query = nullptr;
s_internal std::string gpu_frame_time_query = "frame_gpu";
s_internal std::string gpu_imgui_time_query = "imgui_gpu";
s_internal std::string cpu_wait_time = "cpu_wait";
s_internal std::string cpu_rest_of_frame = "cpu_rest_of_frame";
s_internal std::string cpu_particle_sim = "cpu_particle_sim";

// Particles data
s_internal bool should_reset = false;
s_internal particle::mcallister_system *particle_system = nullptr;

// Camera
s_internal ImVec2 last_mouse_pos = {};
void update_camera();
camera cam = camera((float)g_hwnd_width / (float)g_hwnd_height,
                    0.25f * XM_PI,
                    XMVectorSet(0.f, 0.f, -10.f, 1.f));

s_internal pass_data cb_pass = {};

extern "C" __declspec(dllexport) bool initialize()
{
    if (!XMVerifyCPUSupport())
        return false;

    hr = SetThreadDescription(GetCurrentThread(), L"main_thread");
    ASSERT(SUCCEEDED(hr));

    cam = camera((float)g_hwnd_width / (float)g_hwnd_height,
                 0.25f * XM_PI,
                 XMVectorSet(0.f, 0.f, -10.f, 1.f));

    // Init device resources
    dr = new device_resources();
    device = dr->device;
    cmd_queue = dr->cmd_queue;

    // Init shader objects
    if (!create_shader_objects())
        return false;

    // Init Dear ImGui
    imgui_init(device);

    // Init particles
    particle_system = new particle::mcallister_system(
        new particle::flow(50,
                           {new particle::position<particle::point>(XMFLOAT3(0.0f, 0.0f, 0.15f)),
                            new particle::age<particle::constant>(0.f),
                            //new particle::age<particle::random>({0.f, 5.f}),
                            new particle::velocity<particle::cylinder>({XMVectorSet(0.f, 0.f, 0.85f, 0.f),
                                                                        XMVectorSet(0.f, 0.f, 1.97f, 0.f),
                                                                        0.21f, 0.19f})}),
        {
            new particle::move()
            //new particle::gravity(XMVectorSet(0.f, 0.f, -1.8f, 0.f))
        },
        device);

    // Init command objects
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        frame_resources[i] = new frame_resource(device, i, particle_system->get_frame_partition(i));
    }
    frame = frame_resources[0];

    hr = device->CreateCommandList(DEFAULT_NODE,
                                   D3D12_COMMAND_LIST_TYPE_DIRECT,
                                   frame_resources[0]->cmd_alloc,
                                   nullptr,
                                   IID_PPV_ARGS(&main_cmdlist));
    ASSERT(SUCCEEDED(hr));
    NAME_D3D12_OBJECT(main_cmdlist);

    // Init GPU timer
    query = new gpu_query(device, main_cmdlist, cmd_queue, &dr->backbuffer_index, NUM_QUERIES);

    main_cmdlist->Close();
    cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&main_cmdlist);
    dr->flush_cmd_queue();

    return true;
}

extern "C" __declspec(dllexport) bool update_and_render()
{
    // update
    double dt = timer.tick();

    imgui_update();

    update_camera();
    cam.update_view();
    XMStoreFloat4x4(&cb_pass.view, cam.view);
    XMStoreFloat4x4(&cb_pass.proj, XMMatrixTranspose(cam.proj));

    if (should_reset)
    {
        for (size_t i = 0; i < _countof(frame_resources); i++)
        {
            particle_system->reset(reinterpret_cast<particle::particle>(frame_resources[i]->particle_vb_range));
        }
        should_reset = false;
    }

    particle::particle current_particle = reinterpret_cast<particle::particle>(frame->particle_vb_range);

    timer.start(cpu_particle_sim);
    auto [current_particle_start, current_particle_end] = particle_system->simulate(
        (float)dt,
        current_particle);
    timer.stop(cpu_particle_sim);

    timer.start(cpu_wait_time);
    if (is_waiting_present)
    {
        dr->wait_present();
    }
    UINT backbuffer_index = dr->swapchain->GetCurrentBackBufferIndex(); // gets updated after each call to Present()
    dr->backbuffer_index = backbuffer_index;

    frame = frame_resources[backbuffer_index];
    dr->cpu_wait(frame->fence_value);

    cmd_alloc = frame->cmd_alloc;
    timer.stop(cpu_wait_time);

    timer.start(cpu_rest_of_frame);

    // render
    cmd_alloc->Reset();
    main_cmdlist->Reset(cmd_alloc, flat_color_pso);

    frame->cb_passdata_upload->copy_data(0, (void *)&cb_pass);

    query->start(gpu_frame_time_query);

    main_cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                         dr->main_rt_resources[backbuffer_index],
                                         D3D12_RESOURCE_STATE_PRESENT | D3D12_RESOURCE_STATE_COMMON,
                                         D3D12_RESOURCE_STATE_RENDER_TARGET));

    set_viewport_rects(main_cmdlist);

    PIXBeginEvent(main_cmdlist, 1, "clear_dsv_rtv");
    main_cmdlist->ClearDepthStencilView(dr->dsv_heap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.f, 0, 0, NULL);
    main_cmdlist->ClearRenderTargetView(dr->rtv_descriptors[backbuffer_index], DirectX::Colors::Black, 0, NULL);
    PIXEndEvent(main_cmdlist);

    main_cmdlist->OMSetRenderTargets(1, &dr->rtv_descriptors[backbuffer_index], FALSE, &dr->dsv_heap->GetCPUDescriptorHandleForHeapStart());

    UINT num_vertices = UINT(current_particle_end - current_particle_start);
    if (num_vertices > 0)
    {
        // Configure VBVs from particle pointers
        size_t particle_gpu_data_start = particle_system->m_vertex_upload_resource->m_uploadbuffer->GetGPUVirtualAddress();
        UINT particle_vb_size = (UINT)particle_system->m_vertexbuffer_stride;
        UINT particle_vb_stride = (UINT)particle::size;
        BYTE *particle_cpu_data_start = particle_system->m_vertex_upload_resource->m_mapped_data;

        D3D12_VERTEX_BUFFER_VIEW vbv[4] = {};

        // Position data
        size_t position_offset = (BYTE *)&current_particle_start->position - particle_cpu_data_start;

        vbv[0].BufferLocation = particle_gpu_data_start + position_offset;
        vbv[0].SizeInBytes = particle_vb_size - offsetof(particle::aligned_particle_aos, position);
        vbv[0].StrideInBytes = particle_vb_stride;

        // Color data
        size_t color_offset = (BYTE *)&current_particle_start->color - particle_cpu_data_start;

        vbv[1].BufferLocation = particle_gpu_data_start + color_offset;
        vbv[1].SizeInBytes = particle_vb_size - offsetof(particle::aligned_particle_aos, color);
        vbv[1].StrideInBytes = particle_vb_stride;

        // Velocity data
        size_t velocity_offset = (BYTE *)&current_particle_start->velocity - particle_cpu_data_start;

        vbv[2].BufferLocation = particle_gpu_data_start + velocity_offset;
        vbv[2].SizeInBytes = particle_vb_size - offsetof(particle::aligned_particle_aos, velocity);
        vbv[2].StrideInBytes = particle_vb_stride;

        // Age data
        size_t age_offset = (BYTE *)&current_particle_start->age - particle_cpu_data_start;

        vbv[3].BufferLocation = particle_gpu_data_start + age_offset;
        vbv[3].SizeInBytes = particle_vb_size - offsetof(particle::aligned_particle_aos, age);
        vbv[3].StrideInBytes = particle_vb_stride;

        // Draw particles
        main_cmdlist->SetGraphicsRootSignature(dr->rootsig);
        main_cmdlist->SetGraphicsRootConstantBufferView(0, frame->cb_passdata_upload->m_uploadbuffer->GetGPUVirtualAddress());
        main_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_POINTLIST);
        main_cmdlist->IASetVertexBuffers(0, _countof(vbv), vbv);
        main_cmdlist->DrawInstanced(num_vertices, 1, 0, 0);
    }

    // UI rendering
    PIXBeginEvent(main_cmdlist, 0, gpu_imgui_time_query.c_str());
    query->start(gpu_imgui_time_query);
    imgui_render(main_cmdlist);
    query->stop(gpu_imgui_time_query);
    PIXEndEvent(main_cmdlist);

    main_cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                         dr->main_rt_resources[backbuffer_index],
                                         D3D12_RESOURCE_STATE_RENDER_TARGET,
                                         D3D12_RESOURCE_STATE_PRESENT));

    query->stop(gpu_frame_time_query);
    query->resolve();

    main_cmdlist->Close();
    cmd_queue->ExecuteCommandLists((UINT)1, (ID3D12CommandList *const *)&main_cmdlist);

    dr->present(is_vsync);
    frame->fence_value = dr->signal();

    timer.stop(cpu_rest_of_frame);

    return true;
}

void imgui_update()
{
    imgui_new_frame();
    ImGui::ShowDemoWindow(&show_demo);
    imgui_app_combo();
    imgui_mouse_pos();
    imgui_gpu_memory(dr->adapter);

    ImGui::Checkbox("VSync", &is_vsync);
    ImGui::Checkbox("Wait for Present()", &is_waiting_present);

    ImGui::Text("GPU ImGui time: %f ms", query->result(gpu_imgui_time_query));
    ImGui::Text("GPU frame time: %f ms", query->result(gpu_frame_time_query));
    ImGui::Text("CPU frame time: %f ms", timer.frame_time_ms);
    ImGui::Text("CPU frame cycles: %d cycles", timer.cycles_per_frame);
    ImGui::Text("CPU particle sim: %f ms", timer.result_ms(cpu_particle_sim));
    ImGui::Text("CPU wait time: %f ms", timer.result_ms(cpu_wait_time));
    ImGui::Text("CPU rest of frame time: %f ms", timer.result_ms(cpu_rest_of_frame));
    ImGui::Text("Total frame count: %d", timer.total_frame_count);
    ImGui::Text("FPS: %d", timer.fps);

    ImGui::Separator();

    ImGui::Text("Particles alive: %d / %d", particle_system->m_num_particles_alive, particle_system->m_max_particles_per_frame);
    ImGui::Text("Particles total: %d", particle_system->m_num_particles_total);

    if (ImGui::Button("Reset particle system"))
    {
        should_reset = true;
    }
}

void update_camera()
{
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
}

bool create_shader_objects()
{
    if (!compile_shader(L"..\\..\\particles\\shaders\\default.hlsl", L"VS", shader_type::vertex, &particle_blob_vs))
        return false;
    if (!compile_shader(L"..\\..\\particles\\shaders\\default.hlsl", L"PS", shader_type::pixel, &particle_blob_ps))
        return false;
    if (!compile_shader(L"..\\..\\particles\\shaders\\default.hlsl", L"GS", shader_type::geometry, &particle_blob_gs))
        return false;

    std::vector<CD3DX12_ROOT_PARAMETER1> params;
    // (root) ConstantBuffer<view_proj> cb_viewproj : register(b0);
    CD3DX12_ROOT_PARAMETER1 param_viewproj;
    param_viewproj.InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_VERTEX);
    params.push_back(param_viewproj);

    dr->create_rootsig(&params, L"main_rootsig");

    std::vector<D3D12_INPUT_ELEMENT_DESC> input_elem_descs;
    input_elem_descs.push_back({"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    input_elem_descs.push_back({"COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    input_elem_descs.push_back({"VELOCITY", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    input_elem_descs.push_back({"AGE", 0, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});

    D3D12_GRAPHICS_PIPELINE_STATE_DESC default_pso_desc = dr->create_default_pso_desc(&input_elem_descs);
    default_pso_desc.VS = {particle_blob_vs->GetBufferPointer(), particle_blob_vs->GetBufferSize()};
    default_pso_desc.PS = {particle_blob_ps->GetBufferPointer(), particle_blob_ps->GetBufferSize()};
    default_pso_desc.GS = {particle_blob_gs->GetBufferPointer(), particle_blob_gs->GetBufferSize()};
    default_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    default_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    hr = device->CreateGraphicsPipelineState(&default_pso_desc, IID_PPV_ARGS(&flat_color_pso));
    ASSERT(SUCCEEDED(hr));

    return true;
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
    delete particle_system;
    delete query;
    safe_release(flat_color_pso);
    safe_release(main_cmdlist);
    imgui_shutdown();
    delete dr;

    for (size_t i = 0; i < _countof(frame_resources); i++)
        delete frame_resources[i];

#ifdef DX12_ENABLE_DEBUG_LAYER
    IDXGIDebug1 *debug = NULL;
    if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug))))
    {
        debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_DETAIL);
        safe_release(debug);
    }
#endif
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

        last_mouse_pos = ImGui::GetMousePos();
        break;
    }
}