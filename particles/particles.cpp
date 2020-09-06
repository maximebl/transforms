#include "pch.h"
#include <gpu_interface.h>
#include <imgui_helpers.h>
#include "frame_resource.h"
#include "gpu_query.h"
#include "step_timer.h"
#include "mcallister_system.h"
#include "camera.h"
#include "DDSTextureLoader12.h"

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
void create_shader_objects();
s_internal ID3DBlob *billboard_blob_vs = nullptr;
s_internal ID3DBlob *billboard_blob_ps = nullptr;
s_internal ID3DBlob *billboard_blob_gs = nullptr;

s_internal ID3DBlob *points_blob_vs = nullptr;
s_internal ID3DBlob *points_blob_ps = nullptr;

s_internal ID3D12PipelineState *billboard_pso = nullptr;
s_internal ID3D12PipelineState *point_pso = nullptr;

// Textures
ID3D12DescriptorHeap *srv_heap = nullptr;
void create_texture_objects();
s_internal ID3D12Resource *fire_texture_default_resource = nullptr;
s_internal ID3D12Resource *fire_texture_upload_resource = nullptr;
s_internal material_data cb_material = {};

// Queries data
s_internal constexpr int NUM_QUERIES = 3;
s_internal gpu_query *query = nullptr;
s_internal std::string gpu_frame_time_query = "frame_gpu";
s_internal std::string gpu_particles_time_query = "particles_gpu";
s_internal std::string gpu_imgui_time_query = "imgui_gpu";
s_internal std::string cpu_wait_time = "cpu_wait";
s_internal std::string cpu_rest_of_frame = "cpu_rest_of_frame";
s_internal std::string cpu_particle_sim = "cpu_particle_sim";

// Particles data
s_internal void imgui_particle_mode_combo(int *mode);
s_internal particle::mcallister_system *particle_system = nullptr;
s_internal bool should_reset_particles = false;
enum class particle_rendering_mode
{
    point,
    billboard,
    overdraw
};
particle_rendering_mode rendering_mode = particle_rendering_mode::billboard;

// Camera
s_internal ImVec2 last_mouse_pos = {};
void update_camera_pos();
camera cam = camera((float)g_hwnd_width / (float)g_hwnd_height,
                    0.25f * XM_PI,
                    XMVectorSet(0.f, 0.f, -10.f, 1.f));

s_internal pass_data cb_pass = {};

extern "C" __declspec(dllexport) bool initialize()
{
    if (!XMVerifyCPUSupport())
        return false;

    check_hr(SetThreadDescription(GetCurrentThread(), L"main_thread"));

    cam = camera((float)g_hwnd_width / (float)g_hwnd_height,
                 0.25f * XM_PI,
                 XMVectorSet(0.f, 0.f, -10.f, 1.f));

    // Initialize device resources
    dr = new device_resources();
    device = dr->device;
    cmd_queue = dr->cmd_queue;

    // Initialize Dear ImGui
    imgui_init(device);

    // Initialize particles
    particle_system = new particle::mcallister_system(
        new particle::flow(50.0,
                           {new particle::position<particle::point>(XMFLOAT3(0.0f, -1.5f, 0.0f)),
                            new particle::size<particle::random>({0.1f, 1.f}),
                            //new particle::age<particle::constant>(1.f),
                            new particle::age<particle::random>({0.f, 5.f}),
                            new particle::velocity<particle::cylinder>({XMVectorSet(0.f, 1.f, 0.f, 0.f),
                                                                        XMVectorSet(0.f, 2.f, 0.f, 0.f),
                                                                        0.1f, 0.2f})}),
        {
            new particle::move()
            //new particle::gravity(XMVectorSet(0.f, 0.f, -1.8f, 0.f))
        },
        device);

    // Initialize command objects
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        frame_resources[i] = new frame_resource(device, i, particle_system->get_frame_partition(i));
    }
    frame = frame_resources[0];

    check_hr(device->CreateCommandList(DEFAULT_NODE,
                                       D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       frame_resources[0]->cmd_alloc,
                                       nullptr,
                                       IID_PPV_ARGS(&main_cmdlist)));
    NAME_D3D12_OBJECT(main_cmdlist);

    // Load textures
    create_texture_objects();

    // Compile shaders and create PSOs
    create_shader_objects();

    // Initialize GPU timer
    query = new gpu_query(device, main_cmdlist, cmd_queue, &dr->backbuffer_index, NUM_QUERIES);

    main_cmdlist->Close();
    cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&main_cmdlist);
    dr->flush_cmd_queue();

    return true;
}

extern "C" __declspec(dllexport) bool update_and_render()
{
    //
    // Update
    //
    float dt = (float)timer.tick();

    imgui_update();

    // Update pass data
    update_camera_pos();
    cam.update_view();

    XMStoreFloat3(&cb_pass.eye_pos, cam.position);
    XMStoreFloat4x4(&cb_pass.view, cam.view);
    XMStoreFloat4x4(&cb_pass.proj, XMMatrixTranspose(cam.proj));
    cb_pass.time = (float)timer.total_time;

    // Update material data
    float &tex_u = cb_material.transform(3, 0);
    float &tex_v = cb_material.transform(3, 1);

    // Transform to animate texture coordinates
    //float panning_tex_u = tex_u + (0.2f * dt);
    float panning_tex_v = tex_v + (0.1f * dt);
    XMMATRIX tex_transform = XMMatrixTranslation(tex_u, panning_tex_v, 0.f);
    XMStoreFloat4x4(&cb_material.transform, tex_transform);

    // Transform to animate texture coordinates in the opposite direction, at slightly different speeds
    //float inv_panning_tex_u = -(tex_u + (0.4f * dt));
    float inv_panning_tex_v = -(tex_v + (0.1f * dt));
    XMMATRIX inv_tex_transform = XMMatrixTranslation(tex_u, inv_panning_tex_v, 0.f);
    XMStoreFloat4x4(&cb_material.inv_transform, inv_tex_transform);

    if (should_reset_particles)
    {
        for (size_t i = 0; i < _countof(frame_resources); i++)
        {
            particle_system->reset(reinterpret_cast<particle::particle>(frame_resources[i]->particle_vb_range));
        }
        should_reset_particles = false;
    }

    particle::particle current_particle = reinterpret_cast<particle::particle>(frame->particle_vb_range);

    timer.start(cpu_particle_sim);
    auto [current_particle_start, current_particle_end] = particle_system->simulate(
        dt,
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
    main_cmdlist->Reset(cmd_alloc, nullptr);

    frame->cb_pass_upload->copy_data(0, (void *)&cb_pass);

    material_data cpy = {};
    XMStoreFloat4x4(&cpy.transform, XMMatrixTranspose(tex_transform));
    XMStoreFloat4x4(&cpy.inv_transform, XMMatrixTranspose(inv_tex_transform));
    frame->cb_material_upload->copy_data(0, (void *)&cpy);

    query->start(gpu_frame_time_query);

    main_cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                         dr->back_buffers[backbuffer_index],
                                         D3D12_RESOURCE_STATE_PRESENT | D3D12_RESOURCE_STATE_COMMON,
                                         D3D12_RESOURCE_STATE_RENDER_TARGET));

    set_viewport_rects(main_cmdlist);

    PIXBeginEvent(main_cmdlist, 1, "clear_dsv_rtv");
    main_cmdlist->ClearDepthStencilView(dr->dsv_heap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.f, 0, 0, NULL);
    main_cmdlist->ClearRenderTargetView(dr->rtv_descriptors[backbuffer_index], DirectX::Colors::Black, 0, NULL);
    PIXEndEvent(main_cmdlist);

    main_cmdlist->OMSetRenderTargets(1, &dr->rtv_descriptors[backbuffer_index], FALSE, &dr->dsv_heap->GetCPUDescriptorHandleForHeapStart());

    main_cmdlist->SetGraphicsRootSignature(dr->rootsig);
    main_cmdlist->SetGraphicsRootConstantBufferView(0, frame->cb_pass_upload->m_uploadbuffer->GetGPUVirtualAddress());

    query->start(gpu_particles_time_query);

    UINT num_vertices = UINT(current_particle_end - current_particle_start);
    if (num_vertices > 0)
    {
        // Configure VBVs from particle pointers
        size_t particle_gpu_data_start = particle_system->m_vertex_upload_resource->m_uploadbuffer->GetGPUVirtualAddress();
        UINT particle_vb_size = (UINT)particle_system->m_vertexbuffer_stride;
        UINT particle_vb_stride = (UINT)particle::byte_size;
        BYTE *particle_cpu_data_start = particle_system->m_vertex_upload_resource->m_mapped_data;

        D3D12_VERTEX_BUFFER_VIEW vbv[4] = {};

        // Position data
        size_t position_offset = (BYTE *)&current_particle_start->position - particle_cpu_data_start;

        vbv[0].BufferLocation = particle_gpu_data_start + position_offset;
        vbv[0].SizeInBytes = particle_vb_size - offsetof(particle::aligned_particle_aos, position);
        vbv[0].StrideInBytes = particle_vb_stride;

        // Size data
        size_t size_offset = (BYTE *)&current_particle_start->size - particle_cpu_data_start;

        vbv[1].BufferLocation = particle_gpu_data_start + size_offset;
        vbv[1].SizeInBytes = particle_vb_size - offsetof(particle::aligned_particle_aos, size);
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
        main_cmdlist->SetDescriptorHeaps(1, &srv_heap);
        main_cmdlist->IASetVertexBuffers(0, _countof(vbv), vbv);
        main_cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);
        main_cmdlist->SetGraphicsRootDescriptorTable(1, srv_heap->GetGPUDescriptorHandleForHeapStart());

        switch (rendering_mode)
        {
        case particle_rendering_mode::point:
            main_cmdlist->SetPipelineState(point_pso);
            break;

        case particle_rendering_mode::billboard:
            main_cmdlist->SetPipelineState(billboard_pso);
            break;

        case particle_rendering_mode::overdraw:
            // Not yet implemented. Fallback to billboards
            main_cmdlist->SetPipelineState(billboard_pso);
            break;
        }

        main_cmdlist->DrawInstanced(num_vertices, 1, 0, 0);
    }

    query->stop(gpu_particles_time_query);

    // UI rendering
    PIXBeginEvent(main_cmdlist, 0, gpu_imgui_time_query.c_str());
    query->start(gpu_imgui_time_query);
    imgui_render(main_cmdlist);
    query->stop(gpu_imgui_time_query);
    PIXEndEvent(main_cmdlist);

    main_cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                         dr->back_buffers[backbuffer_index],
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
    ImGui::Text("GPU particles time: %f ms", query->result(gpu_particles_time_query));
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
        should_reset_particles = true;

    imgui_particle_mode_combo((int *)&rendering_mode);
}

void imgui_particle_mode_combo(int *mode)
{
    const char *mode_str[] = {"Point",
                              "Billboard",
                              "Overdraw"};

    const char *current_mode = mode_str[*mode];
    if (ImGui::BeginCombo("Particle rendering mode", current_mode))
    {
        for (int i = 0; i < _countof(mode_str); ++i)
        {
            bool is_selected = (current_mode == mode_str[i]);
            if (ImGui::Selectable(mode_str[i], is_selected))
            {
                current_mode = mode_str[i];
                *mode = i;
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

void update_camera_pos()
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

void create_shader_objects()
{
    compile_shader(L"..\\..\\particles\\shaders\\billboards.hlsl", L"VS", shader_type::vertex, &billboard_blob_vs);
    compile_shader(L"..\\..\\particles\\shaders\\billboards.hlsl", L"PS", shader_type::pixel, &billboard_blob_ps);
    compile_shader(L"..\\..\\particles\\shaders\\billboards.hlsl", L"GS", shader_type::geometry, &billboard_blob_gs);

    compile_shader(L"..\\..\\particles\\shaders\\points.hlsl", L"VS", shader_type::vertex, &points_blob_vs);
    compile_shader(L"..\\..\\particles\\shaders\\points.hlsl", L"PS", shader_type::pixel, &points_blob_ps);

    std::vector<CD3DX12_ROOT_PARAMETER1> params = {};

    // (root) ConstantBuffer<view_proj> cb_viewproj : register(b0);
    CD3DX12_ROOT_PARAMETER1 param_viewproj;
    param_viewproj.InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    params.push_back(param_viewproj);

    // (table) Texture2D srv_fire_texture : register(t0);
    CD3DX12_DESCRIPTOR_RANGE1 mat_ranges[2];
    mat_ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
    mat_ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
    CD3DX12_ROOT_PARAMETER1 param_mat;
    param_mat.InitAsDescriptorTable(_countof(mat_ranges), mat_ranges, D3D12_SHADER_VISIBILITY_ALL);
    params.push_back(param_mat);

    // Samplers
    std::vector<CD3DX12_STATIC_SAMPLER_DESC> samplers = {};

    CD3DX12_STATIC_SAMPLER_DESC linear_sampler_desc = {};
    linear_sampler_desc.Init(0);
    linear_sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    linear_sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers.push_back(linear_sampler_desc);

    dr->create_rootsig(&params, &samplers);

    std::vector<D3D12_INPUT_ELEMENT_DESC> input_elem_descs;
    input_elem_descs.push_back({"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    input_elem_descs.push_back({"SIZE", 0, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    input_elem_descs.push_back({"VELOCITY", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    input_elem_descs.push_back({"AGE", 0, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});

    //
    // Alpha transparency blending
    //
    D3D12_RENDER_TARGET_BLEND_DESC transparent_rtv_blend_desc = {};
    transparent_rtv_blend_desc.BlendEnable = true;
    transparent_rtv_blend_desc.LogicOpEnable = false;
    transparent_rtv_blend_desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // Enable alpha controlled transparency
    // C = (C_src * C_src_alpha) + (C_dst * (1 - C_src_alpha))
    transparent_rtv_blend_desc.BlendOp = D3D12_BLEND_OP_ADD;
    transparent_rtv_blend_desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
    transparent_rtv_blend_desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;

    // Don't blend alpha values
    transparent_rtv_blend_desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
    transparent_rtv_blend_desc.DestBlendAlpha = D3D12_BLEND_ZERO;
    transparent_rtv_blend_desc.SrcBlendAlpha = D3D12_BLEND_ONE;

    D3D12_BLEND_DESC transparency_blend_desc = {};
    transparency_blend_desc.AlphaToCoverageEnable = false;
    transparency_blend_desc.IndependentBlendEnable = false;
    transparency_blend_desc.RenderTarget[0] = transparent_rtv_blend_desc;

    //
    // Additive blending
    //
    D3D12_RENDER_TARGET_BLEND_DESC additive_rtv_blend_desc = transparent_rtv_blend_desc;

    // Enable additive blending
    // C = (C_src * (1,1,1,1)) + (C_dst * (1,1,1,1))
    additive_rtv_blend_desc.BlendOp = D3D12_BLEND_OP_ADD;
    additive_rtv_blend_desc.SrcBlend = D3D12_BLEND_ONE;
    additive_rtv_blend_desc.DestBlend = D3D12_BLEND_ONE;

    D3D12_BLEND_DESC additive_blend_desc = transparency_blend_desc;
    additive_blend_desc.RenderTarget[0] = additive_rtv_blend_desc;

    // Billboard PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC billboard_pso_desc = dr->create_default_pso_desc(&input_elem_descs);

    // Disable depth writes, but still allow for depth testing
    billboard_pso_desc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

    billboard_pso_desc.VS = {billboard_blob_vs->GetBufferPointer(), billboard_blob_vs->GetBufferSize()};
    billboard_pso_desc.PS = {billboard_blob_ps->GetBufferPointer(), billboard_blob_ps->GetBufferSize()};
    billboard_pso_desc.GS = {billboard_blob_gs->GetBufferPointer(), billboard_blob_gs->GetBufferSize()};
    billboard_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    billboard_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    billboard_pso_desc.BlendState = additive_blend_desc;
    check_hr(device->CreateGraphicsPipelineState(&billboard_pso_desc, IID_PPV_ARGS(&billboard_pso)));
    NAME_D3D12_OBJECT(billboard_pso);

    // Point PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC point_pso_desc = dr->create_default_pso_desc(&input_elem_descs);
    point_pso_desc.VS = {points_blob_vs->GetBufferPointer(), points_blob_vs->GetBufferSize()};
    point_pso_desc.PS = {points_blob_ps->GetBufferPointer(), points_blob_ps->GetBufferSize()};
    point_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    point_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    check_hr(device->CreateGraphicsPipelineState(&point_pso_desc, IID_PPV_ARGS(&point_pso)));
    NAME_D3D12_OBJECT(point_pso);
}

void create_texture_objects()
{
    // Read texture data
    // Create an appropriate default resource for that texture data
    const wchar_t *file = L"..\\..\\particles\\textures\\fire.dds";
    std::unique_ptr<uint8_t[]> texture_data(new uint8_t[0]);
    std::vector<D3D12_SUBRESOURCE_DATA> subresource_data = {};
    check_hr(LoadDDSTextureFromFile(device, file, &fire_texture_default_resource, texture_data, subresource_data));

    // Create the upload heap
    UINT64 req_size = GetRequiredIntermediateSize(fire_texture_default_resource, 0, (UINT)subresource_data.size());
    check_hr(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
                                             D3D12_HEAP_FLAG_NONE,
                                             &CD3DX12_RESOURCE_DESC::Buffer(req_size),
                                             D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
                                             IID_PPV_ARGS(&fire_texture_upload_resource)));

    // Copy the texture data into the upload heap
    // Copy to the content of the upload heap into the default heap
    auto rreq_size = UpdateSubresources(main_cmdlist,
                                        fire_texture_default_resource,
                                        fire_texture_upload_resource,
                                        0, 0,
                                        (UINT)subresource_data.size(), subresource_data.data());

    NAME_D3D12_OBJECT(fire_texture_default_resource);
    NAME_D3D12_OBJECT(fire_texture_upload_resource);

    // We had to transition to D3D12_RESOURCE_STATE_COPY_DEST when uploading to the GPU
    // Transition for use in a pixel shader
    main_cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                         fire_texture_default_resource,
                                         D3D12_RESOURCE_STATE_COPY_DEST,
                                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE));

    // Create the heap to hold the SRVs
    D3D12_DESCRIPTOR_HEAP_DESC srv_heap_desc;
    srv_heap_desc.NodeMask = DEFAULT_NODE;
    srv_heap_desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    srv_heap_desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    srv_heap_desc.NumDescriptors = 2;
    check_hr(device->CreateDescriptorHeap(&srv_heap_desc, IID_PPV_ARGS(&srv_heap)));

    // Create the fire texture's SRV
    D3D12_RESOURCE_DESC fire_tex_desc = fire_texture_default_resource->GetDesc();
    D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
    srv_desc.Texture2D.MipLevels = fire_tex_desc.MipLevels;
    srv_desc.Format = fire_tex_desc.Format;
    srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    device->CreateShaderResourceView(fire_texture_default_resource,
                                     &srv_desc,
                                     srv_heap->GetCPUDescriptorHandleForHeapStart());

    // Create the fire texture transform's CBV
    D3D12_CONSTANT_BUFFER_VIEW_DESC tex_transform_cbv_desc = {};
    tex_transform_cbv_desc.BufferLocation = frame->cb_material_upload->m_uploadbuffer->GetGPUVirtualAddress();
    tex_transform_cbv_desc.SizeInBytes = frame->cb_material_upload->m_buffer_size;

    // Place it in the heap directly after the fire texture SRV
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = {};
    cpu_handle.ptr = srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + dr->srv_desc_handle_incr_size;

    device->CreateConstantBufferView(&tex_transform_cbv_desc,
                                     cpu_handle);
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
    safe_release(srv_heap);
    safe_release(billboard_pso);
    safe_release(point_pso);
    safe_release(main_cmdlist);
    safe_release(fire_texture_default_resource);
    safe_release(fire_texture_upload_resource);
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