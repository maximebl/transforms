#include "pch.h"
#include <gpu_interface.h>
#include <imgui_helpers.h>
#include "frame_resource.h"
#include "gpu_query.h"
#include "step_timer.h"
#include "particle_system_oop.h"
#include "camera.h"
#include "DDSTextureLoader12.h"
#include "geometry_helpers.h"
#include "transform.h"
#include "particle_system_gpu.h"
#include "shaders/shader_shared_constants.h"

using namespace DirectX;

extern "C" __declspec(dllexport) bool update_and_render();
extern "C" __declspec(dllexport) void resize(int width, int height);
extern "C" __declspec(dllexport) void cleanup();
extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) bool initialize();

// App state
s_internal void imgui_update();
s_internal bool show_demo = true;
s_internal bool is_vsync = false;
s_internal bool is_waiting_present = true;
s_internal bool is_gpu_culling = true;
s_internal bool show_bounds = true;
s_internal bool use_multiple_particle_systems = true;

// Command objects
s_internal device_resources *dr = nullptr;
s_internal ID3D12Device *device = nullptr;
s_internal frame_resource *frame_resources[NUM_BACK_BUFFERS] = {};
s_internal frame_resource *frame = nullptr;
s_internal ID3D12GraphicsCommandList *main_cmdlist = nullptr;
s_internal ID3D12CommandQueue *cmd_queue = nullptr;
s_internal ID3D12CommandAllocator *cmd_alloc = nullptr;

// Shader objects
enum heap_offsets
{
    heap_offset_srv_fire_texture,
    heap_offset_cbv_texture_transform,
    heap_offset_uav_particle_systems_bounds,
    heap_offset_uav_filtered_simcmds,
    heap_offset_uav_filtered_drawcmds,
    heap_offset_uav_filtered_bounds,
};

void create_shader_objects();
s_internal ID3DBlob *billboard_blob_vs = nullptr;
s_internal ID3DBlob *billboard_blob_ps = nullptr;
s_internal ID3DBlob *billboard_blob_gs = nullptr;

s_internal ID3DBlob *points_blob_vs = nullptr;
s_internal ID3DBlob *points_blob_ps = nullptr;
s_internal ID3DBlob *particle_sim_blob_cs = nullptr;
s_internal ID3DBlob *floorgrid_blob_vs = nullptr;
s_internal ID3DBlob *floorgrid_blob_ps = nullptr;
s_internal ID3DBlob *commands_blob_cs = nullptr;
s_internal ID3DBlob *bounds_blob_vs = nullptr;
s_internal ID3DBlob *bounds_blob_ps = nullptr;

s_internal ID3D12PipelineState *billboard_pso = nullptr;
s_internal ID3D12PipelineState *point_pso = nullptr;
s_internal ID3D12PipelineState *noproj_point_pso = nullptr;
s_internal ID3D12PipelineState *particle_sim_pso = nullptr;
s_internal ID3D12PipelineState *floorgrid_pso = nullptr;
s_internal ID3D12PipelineState *commands_pso = nullptr;
s_internal ID3D12PipelineState *bounds_pso = nullptr;

// Textures
ID3D12DescriptorHeap *srv_heap = nullptr;
void create_texture_objects();
s_internal ID3D12Resource *fire_texture_default_resource = nullptr;
s_internal ID3D12Resource *fire_texture_upload_resource = nullptr;
s_internal material_data cb_material = {};

// Benchmarking
s_internal cpu_timer timer;
s_internal constexpr int NUM_QUERIES = 10;
s_internal gpu_query *query = nullptr;
s_internal std::string gpu_frame_time_query = "frame_gpu";
s_internal std::string gpu_imgui_time_query = "imgui_gpu";
s_internal std::string cpu_wait_time = "cpu_wait";
s_internal std::string cpu_rest_of_frame = "cpu_rest_of_frame";

s_internal std::string gpu_particles_time_query = "particles_gpu";
s_internal std::string gpu_particles_sim_query = "particles_gpu_sim";
s_internal std::string gpu_particles_draw_query = "particles_gpu_draw";

s_internal std::string cpu_particle_sim = "cpu_particle_sim";

// Particles data
s_internal UINT num_particle_systems = 0;

s_internal ID3D12Resource *particle_output_default = nullptr;
s_internal ID3D12Resource *particle_input_default = nullptr;
s_internal particle::particle_system_oop *particle_system = nullptr;
s_internal std::array<particle::aligned_aos, particle_system->m_max_particles_per_frame> *particle_data = nullptr;
s_internal bool should_reset_particles = false;
s_internal model_data cb_model = {};
s_internal float rot_x = 0.f;
s_internal float rot_y = 0.f;
s_internal float rot_z = 0.f;
s_internal XMVECTOR particle_translation = XMVectorSet(0.f, 0.f, 0.f, 0.f);
s_internal XMVECTOR particle_scale = XMVectorSet(1.f, 1.f, 1.f, 0.f);

// new particle data
s_internal std::vector<particle_system_gpu> particle_systems;
s_internal void create_particle_systems_batch();
s_internal void add_particle_system();
s_internal size_t counter_aligned_simcmds_buffer_size = 0;
s_internal size_t counter_aligned_bounds_uav_buffer_size = 0;

// Transition data

// Command signatures for indirect drawing/simulation
s_internal ID3D12CommandSignature *drawing_cmd_sig = nullptr;
s_internal ID3D12CommandSignature *particle_sim_cmd_sig = nullptr;
s_internal ID3D12CommandSignature *bounds_cmd_sig = nullptr;

// Buffer used to group together all of the indirect simulation/drawing commands from all the particle systems
s_internal ID3D12Resource *input_simcmds_grouped_default = nullptr;
s_internal ID3D12Resource *input_simcmds_grouped_upload = nullptr;
s_internal ID3D12Resource *input_drawcmds_grouped_default = nullptr;
s_internal ID3D12Resource *input_drawcmds_grouped_upload = nullptr;
s_internal ID3D12Resource *input_bounds_drawcmds_grouped_default = nullptr;
s_internal ID3D12Resource *input_bounds_drawcmds_grouped_upload = nullptr;

// Buffer used to received the filtered simulation/drawing commands that passed the furstum culling test
s_internal ID3D12Resource *filtered_simcmds_default = nullptr;
s_internal ID3D12Resource *filtered_simcmds_upload = nullptr;
s_internal ID3D12Resource *simcmds_counter = nullptr;
s_internal ID3D12Resource *simcmds_counter_reset = nullptr;
s_internal ID3D12Resource *drawcmds_counter = nullptr;
s_internal ID3D12Resource *drawcmds_counter_reset = nullptr;
s_internal ID3D12Resource *bounds_counter = nullptr;
s_internal ID3D12Resource *bounds_counter_reset = nullptr;
s_internal ID3D12Resource *filtered_drawcmds_default = nullptr;
s_internal ID3D12Resource *filtered_drawcmds_grouped_upload = nullptr;
s_internal ID3D12Resource *cleared_simcmds_default = nullptr;
s_internal ID3D12Resource *filtered_bounds_drawcmds_default = nullptr;
s_internal ID3D12Resource *filtered_bounds_drawcmds_upload = nullptr;

// Same as simulation_cmds_input, but with the particle_input_uav and particle_output_uav buffers swapped
s_internal ID3D12Resource *swap_simcmds_grouped_default = nullptr;
s_internal ID3D12Resource *swap_simcmds_grouped_upload = nullptr;

// Buffers to hold Axis-aligned bounding box data
s_internal ID3D12Resource *particle_system_bounds_vertices_default = nullptr;
s_internal ID3D12Resource *particle_system_bounds_indices_default = nullptr;
s_internal ID3D12Resource *particle_system_bounds_indices_upload = nullptr;

// Buffers used before we did draw/dispatch call filtering
s_internal ID3D12Resource *indirect_drawing_default = nullptr;
s_internal ID3D12Resource *indirect_drawing_upload = nullptr;
s_internal ID3D12Resource *indirect_sim_cmds_output_default = nullptr;
s_internal ID3D12Resource *indirect_sim_cmds_output_upload = nullptr;
s_internal ID3D12Resource *indirect_sim_cmds_input_default = nullptr;
s_internal ID3D12Resource *indirect_sim_cmds_input_upload = nullptr;
s_internal ID3D12Resource *indirect_sim_cmds_swap_default = nullptr;
s_internal ID3D12Resource *indirect_sim_cmds_swap_upload = nullptr;

// Camera
s_internal ImVec2 last_mouse_pos = {};
void update_camera_pos();
camera cam = camera(0.25f * XM_PI, XMVectorSet(0.f, 0.f, -10.f, 1.f));

// Scene
mesh floor_grid = {};
s_internal pass_data cb_pass = {};

// Debugging
IDXGraphicsAnalysis *ga;

extern "C" __declspec(dllexport) bool initialize()
{
    if (!XMVerifyCPUSupport())
        return false;

    check_hr(SetThreadDescription(GetCurrentThread(), L"main_thread"));

    // Initialize the camera
    cam = camera(0.25f * XM_PI, XMVectorSet(0.f, 0.f, -10.f, 1.f));

    // Initialize device resources
    dr = new device_resources();
    device = dr->device;
    cmd_queue = dr->cmd_queue;

    // Initialize Dear ImGui
    imgui_init(device);

    // Initialize particles
    particle_system = new particle::particle_system_oop(
        // The source
        new particle::flow(50.0,
                           {// List of initializers
                            new particle::position<particle::point>(XMFLOAT3(0.0f, -1.5f, 0.0f)),
                            new particle::size<particle::constant>(1.f),
                            new particle::age<particle::constant>(1.f),
                            //new particle::velocity<particle::point>(XMFLOAT3(0.f, 01.f, 0.f))
                            //new particle::size<particle::random>({0.1f, 1.f}),
                            //new particle::age<particle::random>({0.f, 5.f}),
                            new particle::velocity<particle::cylinder>({XMVectorSet(0.f, 1.f, 0.f, 0.f),
                                                                        XMVectorSet(0.f, 2.f, 0.f, 0.f),
                                                                        0.1f, 0.2f})}),
        {
            // List of actions
            new particle::move()
            //new particle::gravity(XMVectorSet(0.f, 0.f, -1.8f, 0.f))
        },
        device);

    // Initialize command objects
    for (UINT i = 0; i < NUM_BACK_BUFFERS; i++)
    {
        frame_resources[i] = new frame_resource(device, i, particle_system->get_frame_partition(i), num_particle_systems_at_launch);
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

    // Create the floor grid data
    mesh_data grid = create_grid(5.f, 5.f, 20, 20, "floor_grid");
    create_mesh_data(device, main_cmdlist, "floor_grid",
                     sizeof(position_color), grid.vertices.size(), grid.vertices.data(),
                     sizeof(WORD), grid.indices.size(), grid.indices.data(),
                     &floor_grid);

    // Create buffer to hold all of the particle_systems AABBs
    size_t bounds_buffer_size = sizeof(box_positions) * num_particle_systems_at_launch;
    check_hr(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                             D3D12_HEAP_FLAG_NONE,
                                             &CD3DX12_RESOURCE_DESC::Buffer(bounds_buffer_size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
                                             D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                                             nullptr,
                                             IID_PPV_ARGS(&particle_system_bounds_vertices_default)));
    NAME_D3D12_OBJECT(particle_system_bounds_vertices_default);

    // Create buffer to hold the bounds indices
    UINT16 bounds_indices[24] = {
        0, 1, 0, 2,
        0, 4, 1, 3,
        1, 5, 2, 3,
        2, 6, 3, 7,
        4, 5, 4, 6,
        5, 7, 6, 7};

    create_default_buffer(device, main_cmdlist,
                          bounds_indices, sizeof(UINT16) * _countof(bounds_indices),
                          &particle_system_bounds_indices_upload, &particle_system_bounds_indices_default,
                          "particle_system_bounds_indices");

    // Create the particle systems
    create_particle_systems_batch();

    // Create UAVs for the resource that require a counter
    D3D12_CPU_DESCRIPTOR_HANDLE handle = {};
    handle.ptr = srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + (dr->srv_desc_handle_incr_size * heap_offset_uav_particle_systems_bounds);

    D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc;
    uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav_desc.Format = particle_system_bounds_vertices_default->GetDesc().Format;
    uav_desc.Buffer.CounterOffsetInBytes = 0;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    uav_desc.Buffer.NumElements = num_particle_systems_at_launch;
    uav_desc.Buffer.StructureByteStride = sizeof(box_positions);
    device->CreateUnorderedAccessView(particle_system_bounds_vertices_default, nullptr, &uav_desc, handle);

    handle.ptr = srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + (dr->srv_desc_handle_incr_size * heap_offset_uav_filtered_simcmds);
    uav_desc.Buffer.NumElements = max_num_particle_systems;
    uav_desc.Buffer.FirstElement = 0;
    uav_desc.Buffer.CounterOffsetInBytes = 0;
    uav_desc.Buffer.StructureByteStride = sizeof(simulation_indirect_command);
    device->CreateUnorderedAccessView(filtered_simcmds_default, simcmds_counter, &uav_desc, handle);

    uav_desc.Buffer.StructureByteStride = sizeof(draw_indirect_command2);
    handle.ptr = srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + (dr->srv_desc_handle_incr_size * heap_offset_uav_filtered_drawcmds);
    device->CreateUnorderedAccessView(filtered_drawcmds_default, drawcmds_counter, &uav_desc, handle);

    uav_desc.Buffer.StructureByteStride = sizeof(bounds_draw_indirect_command);
    handle.ptr = srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + (dr->srv_desc_handle_incr_size * heap_offset_uav_filtered_bounds);
    device->CreateUnorderedAccessView(filtered_bounds_drawcmds_default, bounds_counter, &uav_desc, handle);

    // Indirect drawing command signature
    D3D12_INDIRECT_ARGUMENT_DESC draw_indirect_args[3] = {};
    draw_indirect_args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
    draw_indirect_args[0].VertexBuffer.Slot = 0;
    draw_indirect_args[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
    draw_indirect_args[1].ConstantBufferView.RootParameterIndex = 7;
    draw_indirect_args[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    UINT indirect_arg_size = (UINT)sizeof(draw_indirect_command2);
    D3D12_COMMAND_SIGNATURE_DESC cmdsig_desc = {};
    cmdsig_desc.NodeMask = DEFAULT_NODE;
    cmdsig_desc.NumArgumentDescs = _countof(draw_indirect_args);
    cmdsig_desc.pArgumentDescs = draw_indirect_args;
    cmdsig_desc.ByteStride = indirect_arg_size;

    check_hr(device->CreateCommandSignature(&cmdsig_desc, dr->rootsig, IID_PPV_ARGS(&drawing_cmd_sig)));
    NAME_D3D12_OBJECT(drawing_cmd_sig);

    // Indirect simulation command signature
    D3D12_INDIRECT_ARGUMENT_DESC simulation_indirect_args[3] = {};
    simulation_indirect_args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
    simulation_indirect_args[0].UnorderedAccessView.RootParameterIndex = 2;
    simulation_indirect_args[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
    simulation_indirect_args[1].UnorderedAccessView.RootParameterIndex = 3;
    simulation_indirect_args[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

    indirect_arg_size = (UINT)sizeof(simulation_indirect_command);
    cmdsig_desc.NumArgumentDescs = _countof(simulation_indirect_args);
    cmdsig_desc.pArgumentDescs = simulation_indirect_args;
    cmdsig_desc.ByteStride = indirect_arg_size;

    check_hr(device->CreateCommandSignature(&cmdsig_desc, dr->rootsig, IID_PPV_ARGS(&particle_sim_cmd_sig)));
    NAME_D3D12_OBJECT(particle_sim_cmd_sig);

    // Indirect bounds drawing command signature
    D3D12_INDIRECT_ARGUMENT_DESC bounds_draw_indirect_args[4] = {};
    bounds_draw_indirect_args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_VERTEX_BUFFER_VIEW;
    bounds_draw_indirect_args[0].VertexBuffer.Slot = 0;
    bounds_draw_indirect_args[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_INDEX_BUFFER_VIEW;
    bounds_draw_indirect_args[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
    bounds_draw_indirect_args[2].ConstantBufferView.RootParameterIndex = 7;
    bounds_draw_indirect_args[3].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    cmdsig_desc.NumArgumentDescs = _countof(bounds_draw_indirect_args);
    cmdsig_desc.pArgumentDescs = bounds_draw_indirect_args;
    cmdsig_desc.ByteStride = sizeof(bounds_draw_indirect_command);
    check_hr(device->CreateCommandSignature(&cmdsig_desc, dr->rootsig, IID_PPV_ARGS(&bounds_cmd_sig)));
    NAME_D3D12_OBJECT(bounds_cmd_sig);

    // Execute initialization commands
    main_cmdlist->Close();
    cmd_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&main_cmdlist);
    dr->flush_cmd_queue();

    return true;
}

s_internal void create_particle_systems_batch()
{
    // Create initial particle data
    using particle_buffer = std::vector<particle::aligned_aos>;
    std::unique_ptr<particle_buffer> particle_data = std::make_unique<particle_buffer>(particle_buffer(max_particles_per_system));

    for (UINT i = 0; i < max_particles_per_system; i++)
    {
        particle_data->at(i).position = {random_float(0.f, 1.f),
                                         random_float(0.f, 1.f),
                                         random_float(0.f, 1.f)};
        particle_data->at(i).size = 1.f;
        particle_data->at(i).age = 1.f;
    }

    particle_systems.reserve(num_particle_systems_at_launch);
    for (size_t i = 0; i < num_particle_systems_at_launch; i++)
    {
        D3D12_GPU_VIRTUAL_ADDRESS transforms_data = frame->cb_transforms_upload->m_upload->GetGPUVirtualAddress();
        transforms_data += frame->cb_transforms_upload->m_element_byte_size * i;

        UINT64 bounds_offset = sizeof(box_positions) * i;
        D3D12_GPU_VIRTUAL_ADDRESS bounds_vertices_gpu_va = particle_system_bounds_vertices_default->GetGPUVirtualAddress() + bounds_offset;

        particle_system_gpu system = particle_system_gpu(device, main_cmdlist,
                                                         particle_data.get(),
                                                         transforms_data,
                                                         max_particles_per_system,
                                                         particle_system_bounds_indices_default->GetGPUVirtualAddress(),
                                                         bounds_vertices_gpu_va);
        system.m_transform.set_translation(i * 2.f, 0.f, 0.f);
        particle_systems.push_back(system);
    }

    // Create the buffers that will hold all of the simulation commands
    size_t indirect_sim_size = sizeof(simulation_indirect_command);
    size_t indirect_buffer_size = indirect_sim_size * max_num_particle_systems;
    int empty = 0;
    create_default_buffer(device, main_cmdlist,
                          (void *)&empty, indirect_buffer_size,
                          &input_simcmds_grouped_upload, &input_simcmds_grouped_default, "input_simcmds_grouped");
    create_default_buffer(device, main_cmdlist,
                          (void *)&empty, indirect_buffer_size,
                          &swap_simcmds_grouped_upload, &swap_simcmds_grouped_default, "swap_simcmds_grouped");
    create_default_buffer(device, main_cmdlist,
                          (void *)&empty, indirect_buffer_size,
                          &filtered_simcmds_upload, &filtered_simcmds_default, "filtered_simcmds", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    // Create the buffers that will hold the drawing commands
    size_t indirect_draw_size = sizeof(draw_indirect_command2);
    indirect_buffer_size = indirect_draw_size * max_num_particle_systems;
    create_default_buffer(device, main_cmdlist,
                          (void *)&empty, indirect_buffer_size,
                          &input_drawcmds_grouped_upload, &input_drawcmds_grouped_default, "input_drawcmds_grouped");

    create_default_buffer(device, main_cmdlist,
                          (void *)&empty, indirect_buffer_size,
                          &filtered_drawcmds_grouped_upload, &filtered_drawcmds_default, "filtered_drawcmds", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    size_t indirect_bounds_draw_size = sizeof(bounds_draw_indirect_command);
    indirect_buffer_size = indirect_bounds_draw_size * max_num_particle_systems;
    create_default_buffer(device, main_cmdlist,
                          (void *)&empty, indirect_buffer_size,
                          &input_bounds_drawcmds_grouped_upload, &input_bounds_drawcmds_grouped_default, "input_bounds_drawcmds");
    create_default_buffer(device, main_cmdlist,
                          (void *)&empty, indirect_buffer_size,
                          &filtered_bounds_drawcmds_upload, &filtered_bounds_drawcmds_default, "filtered_bounds_drawcmds", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    // Create the buffers to hold the UAV counter and a reset buffer
    create_default_buffer(device, main_cmdlist,
                          (void *)&empty, sizeof(UINT),
                          &simcmds_counter_reset, &simcmds_counter, "filtered_simcmds_counter", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    create_default_buffer(device, main_cmdlist,
                          (void *)&empty, sizeof(UINT),
                          &drawcmds_counter_reset, &drawcmds_counter, "filtered_drawcmds_counter", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    create_default_buffer(device, main_cmdlist,
                          (void *)&empty, sizeof(UINT),
                          &bounds_counter_reset, &bounds_counter, "bounds_counter", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    // Clear the counters to 0s
    BYTE *mapped_zeros = nullptr;
    simcmds_counter_reset->Map(0, {}, (void **)&mapped_zeros);
    memset(mapped_zeros, 0, sizeof(UINT));
    drawcmds_counter_reset->Map(0, {}, (void **)&mapped_zeros);
    memset(mapped_zeros, 0, sizeof(UINT));
    bounds_counter_reset->Map(0, {}, (void **)&mapped_zeros);
    memset(mapped_zeros, 0, sizeof(UINT));

    // Transition
    std::vector<D3D12_RESOURCE_BARRIER> transitions;
    D3D12_RESOURCE_BARRIER transition;
    transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    for (particle_system_gpu &particle_system : particle_systems)
    {
        transition.Transition.pResource = particle_system.m_indirect_sim_cmds_input_default;
        transitions.push_back(transition);

        transition.Transition.pResource = particle_system.m_indirect_drawing_default;
        transitions.push_back(transition);

        transition.Transition.pResource = particle_system.m_indirect_sim_cmds_swap_default;
        transitions.push_back(transition);

        transition.Transition.pResource = particle_system.m_indirect_drawing_bounds_default;
        transitions.push_back(transition);
    }
    main_cmdlist->ResourceBarrier((UINT)transitions.size(), transitions.data());

    // Put all of the simulation commands into the same default resource
    size_t offset = 0;
    for (particle_system_gpu &particle_system : particle_systems)
    {
        main_cmdlist->CopyBufferRegion(input_simcmds_grouped_default, offset,
                                       particle_system.m_indirect_sim_cmds_input_default, 0,
                                       indirect_sim_size);
        main_cmdlist->CopyBufferRegion(swap_simcmds_grouped_default, offset,
                                       particle_system.m_indirect_sim_cmds_swap_default, 0,
                                       indirect_sim_size);
        offset += indirect_sim_size;
    }

    // Put all of the particle drawing commands into the same default resource
    offset = 0;
    for (particle_system_gpu &particle_system : particle_systems)
    {
        main_cmdlist->CopyBufferRegion(input_drawcmds_grouped_default, offset,
                                       particle_system.m_indirect_drawing_default, 0,
                                       indirect_draw_size);
        offset += indirect_draw_size;
    }

    // Put all of the bounds drawing commands into the same default resource
    offset = 0;
    for (particle_system_gpu &particle_system : particle_systems)
    {
        main_cmdlist->CopyBufferRegion(input_bounds_drawcmds_grouped_default, offset,
                                       particle_system.m_indirect_drawing_bounds_default, 0,
                                       indirect_bounds_draw_size);
        offset += indirect_bounds_draw_size;
    }

    // Transition the buffers of grouped commands to be used as indirect arguments
    transitions.clear();
    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    transition.Transition.pResource = input_simcmds_grouped_default;
    transitions.push_back(transition);

    transition.Transition.pResource = swap_simcmds_grouped_default;
    transitions.push_back(transition);

    transition.Transition.pResource = filtered_simcmds_default;
    transitions.push_back(transition);

    transition.Transition.pResource = input_drawcmds_grouped_default;
    transitions.push_back(transition);

    transition.Transition.pResource = input_bounds_drawcmds_grouped_default;
    transitions.push_back(transition);

    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    transition.Transition.pResource = filtered_drawcmds_default;
    transitions.push_back(transition);
    transition.Transition.pResource = filtered_bounds_drawcmds_default;
    transitions.push_back(transition);

    main_cmdlist->ResourceBarrier((UINT)transitions.size(), transitions.data());
}

s_internal void add_particle_system()
{
}

extern "C" __declspec(dllexport) bool update_and_render()
{
    //
    // Update
    //
    float dt = (float)timer.tick();

    imgui_update();

    // Update camera
    update_camera_pos();
    cam.update_view();

    // Update model data
    for (size_t i = 0; i < particle_systems.size(); i++)
    {
        frame->cb_transforms_upload->copy_data((int)i, (model_data *)&particle_systems[i].m_transform.m_world_transposed);
    }

    // Update pass data
    XMStoreFloat3(&cb_pass.eye_pos, cam.position);
    XMStoreFloat4x4(&cb_pass.view, cam.view);
    XMStoreFloat4x4(&cb_pass.proj, XMMatrixTranspose(cam.proj));
    cb_pass.time = (float)timer.total_time;
    cb_pass.delta_time = dt;
    cb_pass.aspect_ratio = g_aspect_ratio;
    cb_pass.vert_cotangent = cam.proj.r[1].m128_f32[1];

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

    // Update particles
    timer.start(cpu_particle_sim);
    if (particle_system->m_simulation_mode == particle::simulation_mode::cpu)
    {
        particle_system->simulate(dt, frame);
    }
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

    // Render
    cmd_alloc->Reset();
    main_cmdlist->Reset(cmd_alloc, nullptr);

    PIXEndEvent(main_cmdlist);
    PIXBeginEvent(main_cmdlist, 1, (std::string("Frame ") + std::to_string(timer.total_frame_count)).c_str());

    frame->cb_pass_upload->copy_data(0, (void *)&cb_pass);

    material_data transposed_mat = {};
    XMStoreFloat4x4(&transposed_mat.transform, XMMatrixTranspose(tex_transform));
    XMStoreFloat4x4(&transposed_mat.inv_transform, XMMatrixTranspose(inv_tex_transform));
    frame->cb_material_upload->copy_data(0, (void *)&transposed_mat);

    query->start(gpu_frame_time_query);

    main_cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                         dr->back_buffers[backbuffer_index],
                                         D3D12_RESOURCE_STATE_PRESENT | D3D12_RESOURCE_STATE_COMMON,
                                         D3D12_RESOURCE_STATE_RENDER_TARGET));

    set_viewport_rects(main_cmdlist);

    PIXBeginEvent(main_cmdlist, 1, "clear dsv and rtv");
    main_cmdlist->ClearDepthStencilView(dr->dsv_heap->GetCPUDescriptorHandleForHeapStart(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.f, 0, 0, NULL);
    main_cmdlist->ClearRenderTargetView(dr->rtv_descriptor_handles[backbuffer_index], DirectX::Colors::Black, 0, NULL);
    PIXEndEvent(main_cmdlist);

    main_cmdlist->OMSetRenderTargets(1, &dr->rtv_descriptor_handles[backbuffer_index], FALSE, &dr->dsv_heap->GetCPUDescriptorHandleForHeapStart());

    main_cmdlist->SetGraphicsRootSignature(dr->rootsig);
    main_cmdlist->SetComputeRootSignature(dr->rootsig);
    main_cmdlist->SetGraphicsRootConstantBufferView(0, frame->cb_pass_upload->m_uploadbuffer->GetGPUVirtualAddress());

    // Draw floor grid
    PIXBeginEvent(main_cmdlist, 1, "Draw floor grid");
    main_cmdlist->IASetVertexBuffers(0, 1, &floor_grid.resource->vbv);
    main_cmdlist->IASetIndexBuffer(&floor_grid.resource->ibv);
    main_cmdlist->SetPipelineState(floorgrid_pso);
    main_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_LINELIST);
    main_cmdlist->DrawIndexedInstanced(floor_grid.index_count, 1, 0, 0, 0);
    PIXEndEvent(main_cmdlist);

    query->start(gpu_particles_time_query);

    // Draw particles
    main_cmdlist->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_POINTLIST);

    num_particle_systems = (UINT)particle_systems.size();

    std::vector<D3D12_RESOURCE_BARRIER> transitions = std::vector<D3D12_RESOURCE_BARRIER>();
    D3D12_RESOURCE_BARRIER transition;
    transition.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    transition.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    transition.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;

    // Transition to prepare for resetting uav counters
    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    transition.Transition.pResource = simcmds_counter;
    transitions.push_back(transition);

    transition.Transition.pResource = drawcmds_counter;
    transitions.push_back(transition);

    transition.Transition.pResource = bounds_counter;
    transitions.push_back(transition);
    main_cmdlist->ResourceBarrier((UINT)transitions.size(), transitions.data());

    // Reset the counters
    PIXBeginEvent(main_cmdlist, 1, "Reset counters");
    main_cmdlist->CopyResource(simcmds_counter, simcmds_counter_reset);
    main_cmdlist->CopyResource(drawcmds_counter, drawcmds_counter_reset);
    main_cmdlist->CopyResource(bounds_counter, bounds_counter_reset);
    PIXEndEvent(main_cmdlist);

    // Transition to prepare for culling on simulation and draw commands
    transitions.clear();

    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    transition.Transition.pResource = simcmds_counter;
    transitions.push_back(transition);

    transition.Transition.pResource = drawcmds_counter;
    transitions.push_back(transition);

    transition.Transition.pResource = bounds_counter;
    transitions.push_back(transition);

    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    transition.Transition.pResource = filtered_simcmds_default;
    transitions.push_back(transition);

    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    transition.Transition.pResource = filtered_drawcmds_default;
    transitions.push_back(transition);

    transition.Transition.pResource = particle_system_bounds_vertices_default;
    transitions.push_back(transition);

    for (particle_system_gpu &particle_system : particle_systems)
    {
        transition.Transition.pResource = particle_system.m_output_default;
        transitions.push_back(transition);
    }
    main_cmdlist->ResourceBarrier((UINT)transitions.size(), &transitions[0]);

    // GPU frustum culling of simulation and draw commands
    PIXBeginEvent(main_cmdlist, 1, "Frustum culling of commands");
    main_cmdlist->SetPipelineState(commands_pso);
    main_cmdlist->SetDescriptorHeaps(1, &srv_heap);

    main_cmdlist->SetComputeRootShaderResourceView(4, input_simcmds_grouped_default->GetGPUVirtualAddress());
    CD3DX12_GPU_DESCRIPTOR_HANDLE filtered_simcmds_handle = {};
    filtered_simcmds_handle.ptr = srv_heap->GetGPUDescriptorHandleForHeapStart().ptr +
                                  (heap_offset_uav_filtered_simcmds * dr->srv_desc_handle_incr_size);
    main_cmdlist->SetComputeRootDescriptorTable(5, filtered_simcmds_handle);

    main_cmdlist->SetComputeRoot32BitConstant(6, num_particle_systems, 0); // commands_count

    main_cmdlist->SetComputeRootShaderResourceView(9, input_drawcmds_grouped_default->GetGPUVirtualAddress());
    CD3DX12_GPU_DESCRIPTOR_HANDLE drawcmds_handle = {};
    drawcmds_handle.ptr = srv_heap->GetGPUDescriptorHandleForHeapStart().ptr +
                          (heap_offset_uav_filtered_drawcmds * dr->srv_desc_handle_incr_size);
    main_cmdlist->SetComputeRootDescriptorTable(10, drawcmds_handle);

    main_cmdlist->SetComputeRootShaderResourceView(11, input_bounds_drawcmds_grouped_default->GetGPUVirtualAddress());
    main_cmdlist->Dispatch(1, 1, 1);
    PIXEndEvent(main_cmdlist);

    // Indirect particle simulation
    PIXBeginEvent(main_cmdlist, 1, "Particle simulation");
    main_cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                         filtered_simcmds_default,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                         D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));

    main_cmdlist->SetPipelineState(particle_sim_pso);
    CD3DX12_GPU_DESCRIPTOR_HANDLE particle_bounds_handle = {};
    particle_bounds_handle.ptr = srv_heap->GetGPUDescriptorHandleForHeapStart().ptr +
                                 (heap_offset_uav_particle_systems_bounds * dr->srv_desc_handle_incr_size);
    main_cmdlist->SetComputeRootConstantBufferView(0, frame->cb_pass_upload->m_uploadbuffer->GetGPUVirtualAddress());
    main_cmdlist->SetComputeRootDescriptorTable(8, particle_bounds_handle);

    main_cmdlist->ExecuteIndirect(particle_sim_cmd_sig, num_particle_systems,
                                  filtered_simcmds_default, 0,
                                  simcmds_counter, 0);
    PIXEndEvent(main_cmdlist);

    // Indirect particle drawing
    PIXBeginEvent(main_cmdlist, 1, "Particle drawing");
    transitions.clear();
    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    transition.Transition.pResource = filtered_drawcmds_default;
    transitions.push_back(transition);

    transition.Transition.pResource = filtered_bounds_drawcmds_default;
    transitions.push_back(transition);

    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    transition.Transition.pResource = particle_system_bounds_vertices_default;
    transitions.push_back(transition);

    main_cmdlist->ResourceBarrier((UINT)transitions.size(), transitions.data());

    main_cmdlist->SetPipelineState(point_pso);
    main_cmdlist->ExecuteIndirect(drawing_cmd_sig, num_particle_systems,
                                  filtered_drawcmds_default, 0,
                                  drawcmds_counter, 0);
    PIXEndEvent(main_cmdlist);

    // Draw particle system bounds
    if (show_bounds)
    {
        PIXBeginEvent(main_cmdlist, 1, "Particle bounds drawing");
        main_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_LINELIST);
        main_cmdlist->SetPipelineState(bounds_pso);
        main_cmdlist->ExecuteIndirect(bounds_cmd_sig, num_particle_systems,
                                      filtered_bounds_drawcmds_default, 0,
                                      bounds_counter, 0);
        PIXEndEvent(main_cmdlist);
    }

    // Swap command buffers
    ID3D12Resource *tmp = input_simcmds_grouped_default;
    input_simcmds_grouped_default = swap_simcmds_grouped_default;
    swap_simcmds_grouped_default = tmp;

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

    ImGui::Text("CPU frame time: %f ms", timer.frame_time_ms);
    ImGui::Text("CPU frame cycles: %d cycles", timer.cycles_per_frame);
    ImGui::Text("CPU wait time: %f ms", timer.result_ms(cpu_wait_time));
    ImGui::Text("CPU rest of frame time: %f ms", timer.result_ms(cpu_rest_of_frame));
    ImGui::Text("Total frame count: %d", timer.total_frame_count);
    ImGui::Text("FPS: %d", timer.fps);
    ImGui::Text("GPU ImGui time: %f ms", query->result(gpu_imgui_time_query));
    ImGui::Text("GPU frame time: %f ms", query->result(gpu_frame_time_query));

    ImGui::Separator();

    ImGui::Checkbox("GPU particle system culling", &is_gpu_culling);
    ImGui::Checkbox("Use multiple particle systems", &use_multiple_particle_systems);
    ImGui::Text("GPU particles time: %f ms", query->result(gpu_particles_time_query));
    ImGui::Text("GPU particles simulation time: %f ms", query->result(gpu_particles_sim_query));
    ImGui::Text("GPU particles draw time: %f ms", query->result(gpu_particles_draw_query));
    ImGui::Text("CPU particle sim: %f ms", timer.result_ms(cpu_particle_sim));

    ImGui::Spacing();

    ImGui::Text("Particles alive: %d / %d", particle_system->m_num_particles_alive, particle_system->m_max_particles_per_frame);
    ImGui::Text("Particles total: %d", particle_system->m_num_particles_total);

    if (ImGui::Button("Reset particle system"))
        should_reset_particles = true;

    imgui_combobox((int *)&particle_system->m_rendering_mode, {"Point", "Billboard", "Overdraw"}, "Particle rendering mode");
    imgui_combobox((int *)&particle_system->m_simulation_mode, {"CPU", "GPU"}, "Particle simulation mode");

    // Transform
    ImGui::SliderFloat3("Scale", particle_scale.m128_f32, 0.f, 20.f);
    ImGui::SliderFloat3("Translation", particle_translation.m128_f32, -20.f, 20.f);
    ImGui::SliderAngle("Rotation x", &rot_x);
    ImGui::SliderAngle("Rotation y", &rot_y);
    ImGui::SliderAngle("Rotation z", &rot_z);
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
    // Billboards shaders
    compile_shader(L"..\\..\\particles\\shaders\\billboards.hlsl", L"VS", shader_type::vertex, &billboard_blob_vs);
    compile_shader(L"..\\..\\particles\\shaders\\billboards.hlsl", L"PS", shader_type::pixel, &billboard_blob_ps);
    compile_shader(L"..\\..\\particles\\shaders\\billboards.hlsl", L"GS", shader_type::geometry, &billboard_blob_gs);

    // Points shaders
    compile_shader(L"..\\..\\particles\\shaders\\points.hlsl", L"VS", shader_type::vertex, &points_blob_vs);
    compile_shader(L"..\\..\\particles\\shaders\\points.hlsl", L"PS", shader_type::pixel, &points_blob_ps);

    // GPU particle simulation compute shader
    compile_shader(L"..\\..\\particles\\shaders\\particle_sim.hlsl", L"CS", shader_type::compute, &particle_sim_blob_cs);

    // Floor grid shaders
    compile_shader(L"..\\..\\particles\\shaders\\floorgrid.hlsl", L"VS", shader_type::vertex, &floorgrid_blob_vs);
    compile_shader(L"..\\..\\particles\\shaders\\floorgrid.hlsl", L"PS", shader_type::pixel, &floorgrid_blob_ps);

    // Indirect command filtering shader
    compile_shader(L"..\\..\\particles\\shaders\\commands_filter.hlsl", L"CS", shader_type::compute, &commands_blob_cs);

    // Bounding box shader
    compile_shader(L"..\\..\\particles\\shaders\\bounds.hlsl", L"VS", shader_type::vertex, &bounds_blob_vs);
    compile_shader(L"..\\..\\particles\\shaders\\bounds.hlsl", L"PS", shader_type::pixel, &bounds_blob_ps);

    std::vector<CD3DX12_ROOT_PARAMETER1> params = {};

    // (root) ConstantBuffer<pass_data> cb_pass : register(b0);
    CD3DX12_ROOT_PARAMETER1 param_pass;
    param_pass.InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);
    params.push_back(param_pass);

    // (table) Texture2D srv_fire_texture : register(t0);
    // (table) Texture2D cb_material : register(b1);
    CD3DX12_DESCRIPTOR_RANGE1 mat_ranges[2];
    mat_ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
    mat_ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);
    CD3DX12_ROOT_PARAMETER1 param_mat;
    param_mat.InitAsDescriptorTable(_countof(mat_ranges), mat_ranges);
    params.push_back(param_mat);

    // (root) RWStructuredBuffer<particle> : register(u0);
    CD3DX12_ROOT_PARAMETER1 param_output_particle = {};
    param_output_particle.InitAsUnorderedAccessView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE);
    params.push_back(param_output_particle);

    // (root) RWStructuredBuffer<particle> : register(u1);
    CD3DX12_ROOT_PARAMETER1 param_input_particle = {};
    param_input_particle.InitAsUnorderedAccessView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE);
    params.push_back(param_input_particle);

    // (table) StructuredBuffer<sim_indirect_command> : register(t1);
    CD3DX12_ROOT_PARAMETER1 param_in_commands = {};
    param_in_commands.InitAsShaderResourceView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    params.push_back(param_in_commands);

    // (table) AppendWStructuredBuffer<indirect_command> : register(u2);
    CD3DX12_DESCRIPTOR_RANGE1 cmd_ranges[1];
    cmd_ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    CD3DX12_ROOT_PARAMETER1 param_out_commands = {};
    param_out_commands.InitAsDescriptorTable(_countof(cmd_ranges), cmd_ranges);
    params.push_back(param_out_commands);

    // (root) ConstantBuffer<command_info> : register(b2);
    CD3DX12_ROOT_PARAMETER1 param_cmd_info;
    param_cmd_info.InitAsConstants(1, 2);
    params.push_back(param_cmd_info);

    // (root) ConstantBuffer<model> : register(b3);
    CD3DX12_ROOT_PARAMETER1 param_model;
    param_model.InitAsConstantBufferView(3, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    params.push_back(param_model);

    // (table) AppendStructuredBuffer<bound> : register(u3);
    CD3DX12_DESCRIPTOR_RANGE1 uav_range[1];
    uav_range[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 3, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    CD3DX12_ROOT_PARAMETER1 param_bounds;
    param_bounds.InitAsDescriptorTable(_countof(uav_range), uav_range);
    params.push_back(param_bounds);

    // (root) StructuredBuffer<indirect_draw_command> : register(t2);
    CD3DX12_ROOT_PARAMETER1 param_in_drawcmds = {};
    param_in_drawcmds.InitAsShaderResourceView(2, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    params.push_back(param_in_drawcmds);

    // (table) AppendStructuredBuffer<indirect_draw_command> : register(u4);
    CD3DX12_DESCRIPTOR_RANGE1 draw_cmd_ranges[1];
    draw_cmd_ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 4, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    CD3DX12_ROOT_PARAMETER1 param_draw_cmds = {};
    param_draw_cmds.InitAsDescriptorTable(_countof(draw_cmd_ranges), draw_cmd_ranges);
    params.push_back(param_draw_cmds);

    // (root ) StructuredBuffer<bounds_draw_indirect_command> in_bounds_draw_commands : register(t3);
    CD3DX12_ROOT_PARAMETER1 param_input_bounds_drawcmds = {};
    param_input_bounds_drawcmds.InitAsShaderResourceView(3, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    params.push_back(param_input_bounds_drawcmds);

    // Samplers
    std::vector<CD3DX12_STATIC_SAMPLER_DESC> samplers = {};
    CD3DX12_STATIC_SAMPLER_DESC linear_sampler_desc = {};
    linear_sampler_desc.Init(0);
    linear_sampler_desc.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    linear_sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers.push_back(linear_sampler_desc);

    dr->create_rootsig(&params, &samplers);

    std::vector<D3D12_INPUT_ELEMENT_DESC> particle_input_elem_desc;
    particle_input_elem_desc.push_back({"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    particle_input_elem_desc.push_back({"SIZE", 0, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    particle_input_elem_desc.push_back({"VELOCITY", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    particle_input_elem_desc.push_back({"AGE", 0, DXGI_FORMAT_R32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});

    std::vector<D3D12_INPUT_ELEMENT_DESC> mesh_input_elem_desc;
    mesh_input_elem_desc.push_back({"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});
    mesh_input_elem_desc.push_back({"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});

    std::vector<D3D12_INPUT_ELEMENT_DESC> positions_only_desc;
    positions_only_desc.push_back({"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0});

    //
    // Alpha transparency blending (blend)
    //
    D3D12_RENDER_TARGET_BLEND_DESC transparent_rtv_blend_desc = {};
    transparent_rtv_blend_desc.BlendEnable = true;
    transparent_rtv_blend_desc.LogicOpEnable = false;
    transparent_rtv_blend_desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

    // Enable conventional alpha blending
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

    //
    // Blend-Add
    //
    D3D12_RENDER_TARGET_BLEND_DESC blendadd_rtv_blend_desc = transparent_rtv_blend_desc;

    // Enable mixture of conventional alpha blending and additive blending
    // C = (C_src * (1,1,1,1)) + (C_dst * (1 - C_src_alpha))
    blendadd_rtv_blend_desc.BlendOp = D3D12_BLEND_OP_ADD;
    blendadd_rtv_blend_desc.SrcBlend = D3D12_BLEND_ONE;
    blendadd_rtv_blend_desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;

    // Default particle PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC default_particle_pso_desc = dr->create_default_pso_desc(&particle_input_elem_desc);

    // Billboard PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC billboard_pso_desc = default_particle_pso_desc;

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
    D3D12_GRAPHICS_PIPELINE_STATE_DESC point_pso_desc = default_particle_pso_desc;
    point_pso_desc.VS = {points_blob_vs->GetBufferPointer(), points_blob_vs->GetBufferSize()};
    point_pso_desc.PS = {points_blob_ps->GetBufferPointer(), points_blob_ps->GetBufferSize()};
    point_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    point_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    check_hr(device->CreateGraphicsPipelineState(&point_pso_desc, IID_PPV_ARGS(&point_pso)));
    NAME_D3D12_OBJECT(point_pso);

    // Floor grid PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC floorgrid_pso_desc = dr->create_default_pso_desc(&mesh_input_elem_desc);
    floorgrid_pso_desc.PS = {floorgrid_blob_ps->GetBufferPointer(), floorgrid_blob_ps->GetBufferSize()};
    floorgrid_pso_desc.VS = {floorgrid_blob_vs->GetBufferPointer(), floorgrid_blob_vs->GetBufferSize()};
    floorgrid_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    check_hr(device->CreateGraphicsPipelineState(&floorgrid_pso_desc, IID_PPV_ARGS(&floorgrid_pso)));
    NAME_D3D12_OBJECT(floorgrid_pso);

    // Bounding box PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC bounds_pso_desc = dr->create_default_pso_desc(&positions_only_desc);
    bounds_pso_desc.PS = {bounds_blob_ps->GetBufferPointer(), bounds_blob_ps->GetBufferSize()};
    bounds_pso_desc.VS = {bounds_blob_vs->GetBufferPointer(), bounds_blob_vs->GetBufferSize()};
    bounds_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    bounds_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    check_hr(device->CreateGraphicsPipelineState(&bounds_pso_desc, IID_PPV_ARGS(&bounds_pso)));
    NAME_D3D12_OBJECT(bounds_pso);

    // Particle simulation compute PSO
    D3D12_COMPUTE_PIPELINE_STATE_DESC particle_sim_pso_desc = {};
    particle_sim_pso_desc.CS = {particle_sim_blob_cs->GetBufferPointer(), particle_sim_blob_cs->GetBufferSize()};
    particle_sim_pso_desc.pRootSignature = dr->rootsig;
    particle_sim_pso_desc.NodeMask = DEFAULT_NODE;
    particle_sim_pso_desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    check_hr(device->CreateComputePipelineState(&particle_sim_pso_desc, IID_PPV_ARGS(&particle_sim_pso)));
    NAME_D3D12_OBJECT(particle_sim_pso);

    // Indirect commands filtering PSO
    D3D12_COMPUTE_PIPELINE_STATE_DESC commands_filtering_pso_desc = particle_sim_pso_desc;
    commands_filtering_pso_desc.CS = {commands_blob_cs->GetBufferPointer(), commands_blob_cs->GetBufferSize()};
    check_hr(device->CreateComputePipelineState(&commands_filtering_pso_desc, IID_PPV_ARGS(&commands_pso)));
    NAME_D3D12_OBJECT(commands_pso);
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
    UpdateSubresources(main_cmdlist,
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
    srv_heap_desc.NumDescriptors = 6;
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
    tex_transform_cbv_desc.SizeInBytes = (UINT)frame->cb_material_upload->m_buffer_size;

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
    cam.update_projection();
    ImGui_ImplDX12_CreateDeviceObjects();
}

extern "C" __declspec(dllexport) void cleanup()
{
    dr->flush_cmd_queue();
    delete particle_system;
    delete query;
    safe_release(srv_heap);
    safe_release(drawing_cmd_sig);
    safe_release(particle_sim_pso);
    safe_release(commands_pso);
    safe_release(point_pso);
    safe_release(billboard_pso);
    safe_release(floorgrid_pso);
    safe_release(particle_input_default);
    safe_release(particle_output_default);
    safe_release(main_cmdlist);
    safe_release(fire_texture_default_resource);
    safe_release(fire_texture_upload_resource);
    safe_release(indirect_drawing_default);
    safe_release(floor_grid.resource->index_default);
    safe_release(floor_grid.resource->index_upload);
    safe_release(floor_grid.resource->vertex_default);
    safe_release(floor_grid.resource->vertex_upload);
    delete floor_grid.resource;
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