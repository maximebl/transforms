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
#include <numeric>

using namespace DirectX;

extern "C" __declspec(dllexport) bool update_and_render();
extern "C" __declspec(dllexport) void resize(int width, int height);
extern "C" __declspec(dllexport) void cleanup();
extern "C" __declspec(dllexport) void wndproc(UINT msg, WPARAM wParam, LPARAM lParam);
extern "C" __declspec(dllexport) bool initialize();

float accum_1 = 0.f;
float accum_2 = 0.f;
int missed_frames_1 = 0;
int missed_frames_2 = 0;

// App state
s_internal bool is_camera_debug_info_visible = false;
s_internal void imgui_update();
s_internal bool show_demo = true;
s_internal bool is_vsync = true;
s_internal bool is_waiting_present = true;
s_internal bool is_gpu_culling = true;
s_internal bool show_bounds = true;
s_internal bool use_multiple_particle_systems = true;
s_internal bool is_capturing_frames = false;

// Command objects
s_internal device_resources *dr = nullptr;
s_internal ID3D12Device *device = nullptr;
s_internal ID3D12CommandQueue *cmd_queue = nullptr;
s_internal ID3D12CommandAllocator *cmd_alloc = nullptr;
s_internal frame_resource *frame_resources[NUM_BACK_BUFFERS] = {};
s_internal frame_resource *frame = nullptr;
s_internal ID3D12GraphicsCommandList *main_cmdlist = nullptr;

// Copy objects
s_internal ID3D12GraphicsCommandList *reset_cmdlist = nullptr;
s_internal ID3D12CommandAllocator *reset_alloc = nullptr;
s_internal ID3D12CommandQueue *copy_queue = nullptr;
s_internal ID3D12Fence *copy_fence = nullptr;
s_internal UINT64 copy_fence_value = 0;
s_internal HANDLE cpu_copy_wait_event = nullptr;
s_internal bool is_first_reset = true;

// Shader objects
enum heap_offsets
{
    heap_offset_srv_fire_texture,
    heap_offset_cbv_texture_transform,
    heap_offset_uav_particle_systems_bounds,
    heap_offset_uav_filtered_simcmds,
    heap_offset_uav_filtered_drawcmds,
    heap_offset_uav_filtered_bounds_drawcmds,
    heap_offset_uav_filtered_bounds_calccmds,
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
s_internal ID3DBlob *calc_bounds_blob_cs = nullptr;
s_internal ID3DBlob *debug_lines_blob_vs = nullptr;
s_internal ID3DBlob *debug_lines_blob_ps = nullptr;
s_internal ID3DBlob *debug_planes_blob_vs = nullptr;
s_internal ID3DBlob *debug_planes_blob_ps = nullptr;

s_internal ID3D12PipelineState *billboard_pso = nullptr;
s_internal ID3D12PipelineState *point_pso = nullptr;
s_internal ID3D12PipelineState *noproj_point_pso = nullptr;
s_internal ID3D12PipelineState *particle_sim_pso = nullptr;
s_internal ID3D12PipelineState *floorgrid_pso = nullptr;
s_internal ID3D12PipelineState *commands_pso = nullptr;
s_internal ID3D12PipelineState *bounds_pso = nullptr;
s_internal ID3D12PipelineState *calc_bounds_pso = nullptr;
s_internal ID3D12PipelineState *debug_line_pso = nullptr;
s_internal ID3D12PipelineState *debug_plane_pso = nullptr;

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

s_internal particle::particle_system_oop *particle_system = nullptr;
s_internal std::array<particle::aligned_aos, particle_system->m_max_particles_per_frame> *particle_data = nullptr;

// new particle data
s_internal std::vector<particle_system_gpu> particle_systems;
s_internal void create_particle_systems_batch();

// Command signatures for indirect drawing/simulation
s_internal ID3D12CommandSignature *drawing_cmd_sig = nullptr;
s_internal ID3D12CommandSignature *particle_sim_cmd_sig = nullptr;
s_internal ID3D12CommandSignature *bounds_drawing_cmd_sig = nullptr;
s_internal ID3D12CommandSignature *bounds_calc_cmd_sig = nullptr;

// Buffer used to group together all of the indirect simulation/drawing commands from all the particle systems
s_internal ID3D12Resource *input_simcmds_grouped_default = nullptr;
s_internal ID3D12Resource *input_simcmds_grouped_upload = nullptr;
s_internal ID3D12Resource *input_drawcmds_grouped_default = nullptr;
s_internal ID3D12Resource *input_drawcmds_grouped_upload = nullptr;
s_internal ID3D12Resource *input_bounds_drawcmds_grouped_default = nullptr;
s_internal ID3D12Resource *input_bounds_drawcmds_grouped_upload = nullptr;
s_internal ID3D12Resource *input_bounds_calccmds_grouped_default = nullptr;
s_internal ID3D12Resource *input_bounds_calccmds_grouped_upload = nullptr;

// Counters
s_internal ID3D12Resource *simcmds_counter = nullptr;
s_internal ID3D12Resource *simcmds_counter_reset = nullptr;
s_internal ID3D12Resource *drawcmds_counter = nullptr;
s_internal ID3D12Resource *drawcmds_counter_reset = nullptr;
s_internal ID3D12Resource *bounds_draw_counter = nullptr;
s_internal ID3D12Resource *bounds_draw_counter_reset = nullptr;
s_internal ID3D12Resource *bounds_calc_counter = nullptr;
s_internal ID3D12Resource *bounds_calc_counter_reset = nullptr;
s_internal ID3D12Resource *bounds_vertices_counter = nullptr;
s_internal ID3D12Resource *bounds_vertices_counter_reset = nullptr;

// Buffer used to received the filtered simulation/drawing commands that passed the furstum culling test
s_internal ID3D12Resource *filtered_simcmds_default = nullptr;
s_internal ID3D12Resource *filtered_simcmds_upload = nullptr;
s_internal ID3D12Resource *filtered_drawcmds_default = nullptr;
s_internal ID3D12Resource *filtered_drawcmds_grouped_upload = nullptr;
s_internal ID3D12Resource *cleared_simcmds_default = nullptr;
s_internal ID3D12Resource *filtered_bounds_drawcmds_default = nullptr;
s_internal ID3D12Resource *filtered_bounds_drawcmds_upload = nullptr;
s_internal ID3D12Resource *filtered_bounds_calccmds_default = nullptr;
s_internal ID3D12Resource *filtered_bounds_calccmds_upload = nullptr;

// Same as simulation_cmds_input, but with the particle_input_uav and particle_output_uav buffers swapped
s_internal ID3D12Resource *swap_simcmds_grouped_default = nullptr;
s_internal ID3D12Resource *swap_simcmds_grouped_upload = nullptr;

// Buffers to hold Axis-aligned bounding box data
s_internal ID3D12Resource *particle_system_bounds_vertices_default = nullptr;
s_internal ID3D12Resource *particle_system_bounds_indices_default = nullptr;
s_internal ID3D12Resource *particle_system_bounds_indices_upload = nullptr;

// Camera
s_internal ImVec2 last_mouse_pos = {};
enum camera_types
{
    main,
    debug
};
camera_types current_camera = camera_types::main;
s_internal INT num_cameras = 2;
s_internal std::unique_ptr<camera> main_cam = nullptr;
s_internal std::unique_ptr<camera> debug_cam = nullptr;

// Scene
mesh floor_grid = {};
s_internal pass_data cb_pass = {};
s_internal physics cb_physics = {};

// Debugging
struct debug_mesh
{
    UINT vertices_byte_size;
    UINT vertices_byte_offset;
    UINT vertices_count;
    UINT indices_byte_size;
    UINT indices_byte_offset;
    UINT indices_count;
};
s_internal ID3D12Resource *debug_indices_default = nullptr;
s_internal ID3D12Resource *debug_indices_upload = nullptr;
s_internal debug_mesh debugcam_frustum;
s_internal debug_mesh debugcam_frustum_planes;
s_internal debug_mesh maincam_frustum;

extern "C" __declspec(dllexport) bool initialize()
{
    if (!XMVerifyCPUSupport())
        return false;

    check_hr(SetThreadDescription(GetCurrentThread(), L"main_thread"));

    // Initialize the camera
    main_cam = std::make_unique<camera>(transform({0.f, 0.f, -10.f}), 1.f, 1000.f);
    debug_cam = std::make_unique<camera>(transform({0.f, 0.f, 0.f}), 1.f, 5.f);

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

    // Create the main rendering command list
    check_hr(device->CreateCommandList(DEFAULT_NODE,
                                       D3D12_COMMAND_LIST_TYPE_DIRECT,
                                       frame_resources[0]->cmd_alloc,
                                       nullptr,
                                       IID_PPV_ARGS(&main_cmdlist)));
    NAME_D3D12_OBJECT(main_cmdlist);

    // Create the copy objects
    cpu_copy_wait_event = CreateEventEx(NULL, NULL, NULL, EVENT_ALL_ACCESS);

    D3D12_COMMAND_QUEUE_DESC copy_queue_desc = {};
    copy_queue_desc.NodeMask = DEFAULT_NODE;
    copy_queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
    copy_queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
    copy_queue_desc.Type = D3D12_COMMAND_LIST_TYPE_COPY;
    check_hr(device->CreateCommandQueue(&copy_queue_desc, IID_PPV_ARGS(&copy_queue)));
    NAME_D3D12_OBJECT(copy_queue);

    check_hr(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_COPY, IID_PPV_ARGS(&reset_alloc)));
    NAME_D3D12_OBJECT(reset_alloc);

    check_hr(device->CreateCommandList(DEFAULT_NODE,
                                       D3D12_COMMAND_LIST_TYPE_COPY,
                                       reset_alloc,
                                       nullptr,
                                       IID_PPV_ARGS(&reset_cmdlist)));
    NAME_D3D12_OBJECT(reset_cmdlist);

    check_hr(device->CreateFence(copy_fence_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&copy_fence)));
    NAME_D3D12_OBJECT(copy_fence);

    // Load textures
    create_texture_objects();

    // Compile shaders and create PSOs
    create_shader_objects();

    // Initialize GPU timer
    query = new gpu_query(device, main_cmdlist, cmd_queue, &dr->backbuffer_index, NUM_QUERIES);

    // Create indices buffer for debug frustums
    std::vector<UINT16> frustum_indices = {0, 1, 1, 2, 2, 3, 3, 0,
                                           0, 4, 1, 5, 2, 6, 3, 7,
                                           4, 5, 5, 6, 6, 7, 7, 4,
                                           4, 8, 5, 9, 6, 10, 7, 11,
                                           8, 9, 9, 10, 10, 11, 11, 8};
    UINT num_frustum_indices = (UINT)frustum_indices.size();

    std::vector<UINT16> frustum_plane_indices = {
        0,
        1,
        2, // Far plane
        0,
        2,
        3,

        4,
        5,
        6, // Projection plane
        4,
        6,
        7,

        8,
        9,
        10, // Near plane
        8,
        10,
        11,

        2,
        3,
        10, // Left plane
        10,
        3,
        11,

        1,
        0,
        9, // Right plane
        9,
        0,
        8,

        1,
        2,
        10, // Top plane
        1,
        10,
        9,

        0,
        3,
        11, // Bottom plane
        0,
        11,
        8,

    };

    UINT num_frustum_plane_indices = (UINT)frustum_plane_indices.size();

    std::vector<UINT16> indices(0);
    indices.reserve(num_frustum_indices + num_frustum_plane_indices);
    UINT indices_byte_offset = 0;
    UINT index_size = sizeof(UINT16);

    // Debug camera frustum
    indices.insert(indices.begin(),
                   frustum_indices.begin(), frustum_indices.end());
    debugcam_frustum.indices_count = num_frustum_indices;
    debugcam_frustum.indices_byte_offset = indices_byte_offset;
    debugcam_frustum.indices_byte_size = index_size * debugcam_frustum.indices_count;

    maincam_frustum.indices_count = num_frustum_indices;
    maincam_frustum.indices_byte_offset = indices_byte_offset;
    maincam_frustum.indices_byte_size = index_size * maincam_frustum.indices_count;

    indices_byte_offset += maincam_frustum.indices_byte_size;

    // Debug camera frustum planes indices
    indices.insert(indices.end(),
                   frustum_plane_indices.begin(), frustum_plane_indices.end());

    debugcam_frustum_planes.indices_count = num_frustum_plane_indices;
    debugcam_frustum_planes.indices_byte_offset = indices_byte_offset;
    debugcam_frustum_planes.indices_byte_size = index_size * debugcam_frustum_planes.indices_count;

    create_default_buffer(device, main_cmdlist,
                          indices.data(), index_size * indices.size(),
                          &debug_indices_upload, &debug_indices_default,
                          "debug_indices");

    // Create the floor grid data
    mesh_data grid = create_grid(5.f, 5.f, 20, 20, "floor_grid");
    create_mesh_data(device, main_cmdlist, "floor_grid",
                     sizeof(position_color), grid.vertices.size(), grid.vertices.data(),
                     sizeof(WORD), grid.indices.size(), grid.indices.data(),
                     &floor_grid);

    // Create buffer to hold all of the particle_systems AABBs
    size_t bounds_buffer_size = sizeof(bounding_box) * num_particle_systems_at_launch;
    check_hr(device->CreateCommittedResource(&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
                                             D3D12_HEAP_FLAG_NONE,
                                             &CD3DX12_RESOURCE_DESC::Buffer(bounds_buffer_size, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS),
                                             D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,
                                             nullptr,
                                             IID_PPV_ARGS(&particle_system_bounds_vertices_default)));
    NAME_D3D12_OBJECT(particle_system_bounds_vertices_default);

    // Create buffer to hold the particle bounds indices
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
    uav_desc.Buffer.StructureByteStride = sizeof(bounding_box);
    device->CreateUnorderedAccessView(particle_system_bounds_vertices_default, bounds_vertices_counter, &uav_desc, handle);

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
    handle.ptr = srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + (dr->srv_desc_handle_incr_size * heap_offset_uav_filtered_bounds_drawcmds);
    device->CreateUnorderedAccessView(filtered_bounds_drawcmds_default, bounds_draw_counter, &uav_desc, handle);

    uav_desc.Buffer.StructureByteStride = sizeof(bounds_calc_indirect_command);
    handle.ptr = srv_heap->GetCPUDescriptorHandleForHeapStart().ptr + (dr->srv_desc_handle_incr_size * heap_offset_uav_filtered_bounds_calccmds);
    device->CreateUnorderedAccessView(filtered_bounds_calccmds_default, bounds_calc_counter, &uav_desc, handle);

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
    D3D12_INDIRECT_ARGUMENT_DESC simulation_indirect_args[5] = {};
    simulation_indirect_args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
    simulation_indirect_args[0].UnorderedAccessView.RootParameterIndex = 2;
    simulation_indirect_args[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
    simulation_indirect_args[1].UnorderedAccessView.RootParameterIndex = 3;
    simulation_indirect_args[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
    simulation_indirect_args[2].ConstantBufferView.RootParameterIndex = 15;
    simulation_indirect_args[3].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT;
    simulation_indirect_args[3].Constant.DestOffsetIn32BitValues = 0;
    simulation_indirect_args[3].Constant.Num32BitValuesToSet = 2;
    simulation_indirect_args[3].Constant.RootParameterIndex = 16;
    simulation_indirect_args[4].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

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
    check_hr(device->CreateCommandSignature(&cmdsig_desc, dr->rootsig, IID_PPV_ARGS(&bounds_drawing_cmd_sig)));
    NAME_D3D12_OBJECT(bounds_drawing_cmd_sig);

    // Bounds calculation command signature
    D3D12_INDIRECT_ARGUMENT_DESC bounds_calc_indirect_args[3] = {};
    bounds_calc_indirect_args[0].Type = D3D12_INDIRECT_ARGUMENT_TYPE_UNORDERED_ACCESS_VIEW;
    bounds_calc_indirect_args[0].UnorderedAccessView.RootParameterIndex = 2;
    bounds_calc_indirect_args[1].Type = D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT_BUFFER_VIEW;
    bounds_calc_indirect_args[1].ConstantBufferView.RootParameterIndex = 7;
    bounds_calc_indirect_args[2].Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

    cmdsig_desc.NumArgumentDescs = _countof(bounds_calc_indirect_args);
    cmdsig_desc.pArgumentDescs = bounds_calc_indirect_args;
    cmdsig_desc.ByteStride = sizeof(bounds_calc_indirect_command);
    check_hr(device->CreateCommandSignature(&cmdsig_desc, dr->rootsig, IID_PPV_ARGS(&bounds_calc_cmd_sig)));
    NAME_D3D12_OBJECT(bounds_calc_cmd_sig);

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
        particle_data->at(i).velocity = XMFLOAT3(
            random_float(-20.f, 20.f),
            random_float(5.f, 25.f),
            random_float(-25.f, 25.f));
    }

    particle_systems.reserve(num_particle_systems_at_launch);
    for (size_t i = 0; i < num_particle_systems_at_launch; i++)
    {
        D3D12_GPU_VIRTUAL_ADDRESS transforms_gpu_va = frame->cb_transforms_upload->m_upload->GetGPUVirtualAddress();
        transforms_gpu_va += frame->cb_transforms_upload->m_element_byte_size * i;

        D3D12_GPU_VIRTUAL_ADDRESS physics_gpu_va = frame->cb_physics->m_upload->GetGPUVirtualAddress();

        D3D12_GPU_VIRTUAL_ADDRESS ps_bounds_indices_gpu_va = particle_system_bounds_indices_default->GetGPUVirtualAddress();

        size_t bounds_offset = (sizeof(bounding_box) * i) + offsetof(bounding_box, positions);
        D3D12_GPU_VIRTUAL_ADDRESS ps_bounds_vertices_gpu_va = particle_system_bounds_vertices_default->GetGPUVirtualAddress() + bounds_offset;

        particle_system_gpu system = particle_system_gpu(device, main_cmdlist,
                                                         particle_data.get(),
                                                         transforms_gpu_va,
                                                         physics_gpu_va,
                                                         ps_bounds_indices_gpu_va,
                                                         ps_bounds_vertices_gpu_va,
                                                         max_particles_per_system);
        system.m_transform.set_translation(i * 2.f, 0.f, 0.f);
        particle_systems.push_back(system);
    }

    // Create the buffers that will hold all of the simulation commands
    size_t indirect_sim_size = sizeof(simulation_indirect_command);
    size_t indirect_buffer_size = indirect_sim_size * max_num_particle_systems;
    create_default_buffer(device, main_cmdlist,
                          nullptr, indirect_buffer_size,
                          &input_simcmds_grouped_upload, &input_simcmds_grouped_default,
                          "input_simcmds_grouped");
    create_default_buffer(device, main_cmdlist,
                          nullptr, indirect_buffer_size,
                          &swap_simcmds_grouped_upload, &swap_simcmds_grouped_default,
                          "swap_simcmds_grouped");
    create_default_buffer(device, main_cmdlist,
                          nullptr, indirect_buffer_size,
                          &filtered_simcmds_upload, &filtered_simcmds_default,
                          "filtered_simcmds", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    // Create the buffers that will hold the drawing commands
    size_t indirect_draw_size = sizeof(draw_indirect_command2);
    indirect_buffer_size = indirect_draw_size * max_num_particle_systems;
    create_default_buffer(device, main_cmdlist,
                          nullptr, indirect_buffer_size,
                          &input_drawcmds_grouped_upload, &input_drawcmds_grouped_default,
                          "input_drawcmds_grouped");

    create_default_buffer(device, main_cmdlist,
                          nullptr, indirect_buffer_size,
                          &filtered_drawcmds_grouped_upload, &filtered_drawcmds_default,
                          "filtered_drawcmds", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    // Create the buffers that will hold the bounds drawing commands
    size_t indirect_bounds_draw_size = sizeof(bounds_draw_indirect_command);
    indirect_buffer_size = indirect_bounds_draw_size * max_num_particle_systems;
    create_default_buffer(device, main_cmdlist,
                          nullptr, indirect_buffer_size,
                          &input_bounds_drawcmds_grouped_upload, &input_bounds_drawcmds_grouped_default, "input_bounds_drawcmds");
    create_default_buffer(device, main_cmdlist,
                          nullptr, indirect_buffer_size,
                          &filtered_bounds_drawcmds_upload, &filtered_bounds_drawcmds_default, "filtered_bounds_drawcmds", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    // Create the buffers that will hold the bounds calculation commands
    size_t indirect_bounds_calc_size = sizeof(bounds_calc_indirect_command);
    indirect_buffer_size = indirect_bounds_calc_size * max_num_particle_systems;
    create_default_buffer(device, main_cmdlist,
                          nullptr, indirect_buffer_size,
                          &input_bounds_calccmds_grouped_upload, &input_bounds_calccmds_grouped_default, "input_bounds_calccmds");
    create_default_buffer(device, main_cmdlist,
                          nullptr, indirect_buffer_size,
                          &filtered_bounds_calccmds_upload, &filtered_bounds_calccmds_default, "filtered_bounds_calccmds", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    // Create the buffers to hold the UAV counter and a reset buffer
    create_default_buffer(device, main_cmdlist,
                          nullptr, sizeof(UINT),
                          &bounds_vertices_counter_reset, &bounds_vertices_counter, "filtered_simcmds_counter", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    create_default_buffer(device, main_cmdlist,
                          nullptr, sizeof(UINT),
                          &simcmds_counter_reset, &simcmds_counter, "filtered_simcmds_counter", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    create_default_buffer(device, main_cmdlist,
                          nullptr, sizeof(UINT),
                          &drawcmds_counter_reset, &drawcmds_counter, "filtered_drawcmds_counter", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    create_default_buffer(device, main_cmdlist,
                          nullptr, sizeof(UINT),
                          &bounds_draw_counter_reset, &bounds_draw_counter, "bounds_draw_counter", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    create_default_buffer(device, main_cmdlist,
                          nullptr, sizeof(UINT),
                          &bounds_calc_counter_reset, &bounds_calc_counter, "bounds_calc_counter", D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);

    // Clear the counters used to reset other counters to 0s
    BYTE *mapped_zeros = nullptr;
    bounds_vertices_counter_reset->Map(0, {}, (void **)&mapped_zeros);
    memset(mapped_zeros, 0, sizeof(UINT));
    simcmds_counter_reset->Map(0, {}, (void **)&mapped_zeros);
    memset(mapped_zeros, 0, sizeof(UINT));
    drawcmds_counter_reset->Map(0, {}, (void **)&mapped_zeros);
    memset(mapped_zeros, 0, sizeof(UINT));
    bounds_draw_counter_reset->Map(0, {}, (void **)&mapped_zeros);
    memset(mapped_zeros, 0, sizeof(UINT));
    bounds_calc_counter_reset->Map(0, {}, (void **)&mapped_zeros);
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

        transition.Transition.pResource = particle_system.m_indirect_bounds_calc_default;
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

    // Put all of the bounds calculation commands into the same default resource
    offset = 0;
    for (particle_system_gpu &particle_system : particle_systems)
    {
        main_cmdlist->CopyBufferRegion(input_bounds_calccmds_grouped_default, offset,
                                       particle_system.m_indirect_bounds_calc_default, 0,
                                       indirect_bounds_calc_size);
        offset += indirect_bounds_calc_size;
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

    transition.Transition.pResource = input_bounds_calccmds_grouped_default;
    transitions.push_back(transition);

    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    transition.Transition.pResource = filtered_drawcmds_default;
    transitions.push_back(transition);
    transition.Transition.pResource = filtered_bounds_drawcmds_default;
    transitions.push_back(transition);

    main_cmdlist->ResourceBarrier((UINT)transitions.size(), transitions.data());
}

extern "C" __declspec(dllexport) bool update_and_render()
{
    //
    // Update
    //
    //float dt = (float)timer.tick();
    float dt = 1.f/144.f;

    imgui_update();

    // Update camera
    switch (current_camera)
    {
    case main:
        main_cam->update_position();
        main_cam->update_view();
        break;
    case debug:
        debug_cam->update_position();
        debug_cam->update_view();
        break;
    }

    particle_systems[0].m_transform.set_translation(0.f, 0.f, 15.f);

    // Update model data
    for (size_t i = 0; i < particle_systems.size(); i++)
    {
        frame->cb_transforms_upload->copy_data((int)i, (model_data *)&particle_systems[i].m_transform.m_world_transposed);
    }

    // Debug camera frustum
    std::vector<position_color> debug_vertices(0);
    UINT vertices_byte_offset = 0;
    UINT vertex_size = sizeof(position_color);

    std::vector<position_color> debugcam_frustum_positions = debug_cam->calc_frustum_vertices();
    debugcam_frustum.vertices_count = (UINT)debugcam_frustum_positions.size();
    debugcam_frustum.vertices_byte_offset = vertices_byte_offset;
    debugcam_frustum.vertices_byte_size = vertex_size * debugcam_frustum.vertices_count;

    // Debug camera frustum planes
    debugcam_frustum_planes.vertices_count = (UINT)debugcam_frustum_positions.size();
    debugcam_frustum_planes.vertices_byte_offset = vertices_byte_offset;
    debugcam_frustum_planes.vertices_byte_size = vertex_size * debugcam_frustum_planes.vertices_count;

    debug_vertices.insert(debug_vertices.end(),
                          debugcam_frustum_positions.begin(), debugcam_frustum_positions.end());

    vertices_byte_offset += debugcam_frustum.vertices_byte_size;

    // Main camera frustum
    std::vector<position_color> maincam_frustum_positions = main_cam->calc_frustum_vertices();
    maincam_frustum.vertices_count = (UINT)maincam_frustum_positions.size();
    maincam_frustum.vertices_byte_offset = vertices_byte_offset;
    maincam_frustum.vertices_byte_size = vertex_size * maincam_frustum.vertices_count;

    debug_vertices.insert(debug_vertices.end(),
                          maincam_frustum_positions.begin(), maincam_frustum_positions.end());

    for (size_t i = 0; i < debug_vertices.size(); i++)
    {
        frame->cb_debug_vertices_upload->copy_data((int)i, &debug_vertices[i]);
    }

    // Update pass data
    switch (current_camera)
    {
    case main:
        cb_pass.eye_pos = main_cam->m_transform.m_translation;
        cb_pass.view = main_cam->m_inv_view;
        XMStoreFloat4x4(&cb_pass.proj, XMMatrixTranspose(XMLoadFloat4x4(&main_cam->m_proj)));

        // Pass the other camera's planes
        cb_pass.frustum_planes[0] = main_cam->m_far_plane;
        cb_pass.frustum_planes[1] = main_cam->m_near_plane;
        cb_pass.frustum_planes[2] = main_cam->m_bottom_plane;
        cb_pass.frustum_planes[3] = main_cam->m_top_plane;
        cb_pass.frustum_planes[4] = main_cam->m_left_plane;
        cb_pass.frustum_planes[5] = main_cam->m_right_plane;
        break;
    case debug:
        cb_pass.eye_pos = debug_cam->m_transform.m_translation;
        cb_pass.view = debug_cam->m_inv_view;
        XMStoreFloat4x4(&cb_pass.proj, XMMatrixTranspose(XMLoadFloat4x4(&debug_cam->m_proj)));

        // Pass the other camera's planes
        cb_pass.frustum_planes[0] = debug_cam->m_bottom_plane;
        cb_pass.frustum_planes[1] = debug_cam->m_top_plane;
        cb_pass.frustum_planes[2] = debug_cam->m_far_plane;
        cb_pass.frustum_planes[3] = debug_cam->m_near_plane;
        cb_pass.frustum_planes[4] = debug_cam->m_left_plane;
        cb_pass.frustum_planes[5] = debug_cam->m_right_plane;
        break;
    }
    cb_pass.time = (float)timer.total_time;
    cb_pass.delta_time = dt;
    cb_pass.aspect_ratio = g_aspect_ratio;

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

    // Update physics data
    frame->cb_physics->copy_data(0, &cb_physics);

    // Update particles
    //timer.start(cpu_particle_sim);
    //if (particle_system->m_simulation_mode == particle::simulation_mode::cpu)
    //{
    //    particle_system->simulate(dt, frame);
    //}
    //timer.stop(cpu_particle_sim);

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

    frame->cb_pass_upload->copy_data(0, &cb_pass);

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
    main_cmdlist->SetGraphicsRootConstantBufferView(0, frame->cb_pass_upload->m_upload->GetGPUVirtualAddress());

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

    transition.Transition.pResource = bounds_draw_counter;
    transitions.push_back(transition);

    transition.Transition.pResource = bounds_calc_counter;
    transitions.push_back(transition);

    transition.Transition.pResource = bounds_vertices_counter;
    transitions.push_back(transition);
    main_cmdlist->ResourceBarrier((UINT)transitions.size(), transitions.data());

    // Reset the counters
    PIXBeginEvent(main_cmdlist, 1, "Reset counters");
    main_cmdlist->CopyResource(bounds_vertices_counter, bounds_vertices_counter_reset);
    main_cmdlist->CopyResource(simcmds_counter, simcmds_counter_reset);
    main_cmdlist->CopyResource(drawcmds_counter, drawcmds_counter_reset);
    main_cmdlist->CopyResource(bounds_draw_counter, bounds_draw_counter_reset);
    main_cmdlist->CopyResource(bounds_calc_counter, bounds_calc_counter_reset);
    PIXEndEvent(main_cmdlist);

    // Transition to prepare for culling on simulation and draw commands
    transitions.clear();

    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    transition.Transition.pResource = bounds_vertices_counter;
    transitions.push_back(transition);

    transition.Transition.pResource = simcmds_counter;
    transitions.push_back(transition);

    transition.Transition.pResource = drawcmds_counter;
    transitions.push_back(transition);

    transition.Transition.pResource = bounds_draw_counter;
    transitions.push_back(transition);

    transition.Transition.pResource = bounds_calc_counter;
    transitions.push_back(transition);

    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    transition.Transition.pResource = filtered_simcmds_default;
    transitions.push_back(transition);

    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    transition.Transition.pResource = filtered_drawcmds_default;
    transitions.push_back(transition);

    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    transition.Transition.pResource = particle_system_bounds_vertices_default;
    transitions.push_back(transition);

    transition.Transition.StateBefore = D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT;
    transition.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    transition.Transition.pResource = filtered_bounds_calccmds_default;
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

    main_cmdlist->SetComputeRootConstantBufferView(0, frame->cb_pass_upload->m_upload->GetGPUVirtualAddress());
    main_cmdlist->SetComputeRoot32BitConstant(6, num_particle_systems, 0); // commands_count

    //main_cmdlist->SetComputeRootShaderResourceView(4, input_simcmds_grouped_default->GetGPUVirtualAddress());
    main_cmdlist->SetComputeRootUnorderedAccessView(4, input_simcmds_grouped_default->GetGPUVirtualAddress());

    main_cmdlist->SetComputeRootShaderResourceView(13, input_bounds_calccmds_grouped_default->GetGPUVirtualAddress());
    CD3DX12_GPU_DESCRIPTOR_HANDLE filtered_simcmds_handle = {};
    filtered_simcmds_handle.ptr = srv_heap->GetGPUDescriptorHandleForHeapStart().ptr +
                                  (heap_offset_uav_filtered_simcmds * dr->srv_desc_handle_incr_size);
    main_cmdlist->SetComputeRootDescriptorTable(5, filtered_simcmds_handle);

    main_cmdlist->SetComputeRootShaderResourceView(9, input_drawcmds_grouped_default->GetGPUVirtualAddress());
    CD3DX12_GPU_DESCRIPTOR_HANDLE drawcmds_handle = {};
    drawcmds_handle.ptr = srv_heap->GetGPUDescriptorHandleForHeapStart().ptr +
                          (heap_offset_uav_filtered_drawcmds * dr->srv_desc_handle_incr_size);
    main_cmdlist->SetComputeRootDescriptorTable(10, drawcmds_handle);

    CD3DX12_GPU_DESCRIPTOR_HANDLE particle_bounds_calc_handle = {};
    particle_bounds_calc_handle.ptr = srv_heap->GetGPUDescriptorHandleForHeapStart().ptr +
                                      (heap_offset_uav_filtered_bounds_calccmds * dr->srv_desc_handle_incr_size);
    main_cmdlist->SetComputeRootDescriptorTable(14, particle_bounds_calc_handle);

    main_cmdlist->SetComputeRootShaderResourceView(11, input_bounds_drawcmds_grouped_default->GetGPUVirtualAddress());

    main_cmdlist->SetComputeRootShaderResourceView(12, particle_system_bounds_vertices_default->GetGPUVirtualAddress());
    main_cmdlist->Dispatch(1, 1, 1);
    PIXEndEvent(main_cmdlist);

    // Indirect particle simulation
    PIXBeginEvent(main_cmdlist, 1, "Particle simulation");
    main_cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                         particle_system_bounds_vertices_default,
                                         D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
    main_cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                         filtered_simcmds_default,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                         D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));

    main_cmdlist->SetPipelineState(particle_sim_pso);

    CD3DX12_GPU_DESCRIPTOR_HANDLE particle_bounds_handle = {};
    particle_bounds_handle.ptr = srv_heap->GetGPUDescriptorHandleForHeapStart().ptr +
                                 (heap_offset_uav_particle_systems_bounds * dr->srv_desc_handle_incr_size);
    main_cmdlist->SetComputeRootDescriptorTable(8, particle_bounds_handle);

    main_cmdlist->ExecuteIndirect(particle_sim_cmd_sig, num_particle_systems,
                                  filtered_simcmds_default, 0,
                                  simcmds_counter, 0);
    PIXEndEvent(main_cmdlist);

    // Calculate particle bounding boxes
    PIXBeginEvent(main_cmdlist, 1, "Particle bounds calculation");
    main_cmdlist->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                         filtered_bounds_calccmds_default,
                                         D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                         D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT));
    main_cmdlist->SetPipelineState(calc_bounds_pso);
    main_cmdlist->ExecuteIndirect(bounds_calc_cmd_sig, num_particle_systems,
                                  filtered_bounds_calccmds_default, 0,
                                  bounds_calc_counter, 0);
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
        main_cmdlist->ExecuteIndirect(bounds_drawing_cmd_sig, num_particle_systems,
                                      filtered_bounds_drawcmds_default, 0,
                                      bounds_draw_counter, 0);
        PIXEndEvent(main_cmdlist);
    }

    // Swap particle simulation command buffers
    ID3D12Resource *tmp = input_simcmds_grouped_default;
    input_simcmds_grouped_default = swap_simcmds_grouped_default;
    swap_simcmds_grouped_default = tmp;

    query->stop(gpu_particles_time_query);

    // Draw debug objects
    D3D12_GPU_VIRTUAL_ADDRESS vertices_gpu_va = frame->cb_debug_vertices_upload->m_upload->GetGPUVirtualAddress();
    D3D12_GPU_VIRTUAL_ADDRESS indices_gpu_va = debug_indices_default->GetGPUVirtualAddress();
    main_cmdlist->SetPipelineState(debug_line_pso);
    main_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_LINELIST);

    // Debug cam
    if (is_camera_debug_info_visible)
    {
        D3D12_VERTEX_BUFFER_VIEW debug_vbv;
        debug_vbv.SizeInBytes = debugcam_frustum.vertices_byte_size;
        debug_vbv.StrideInBytes = sizeof(position_color);
        debug_vbv.BufferLocation = vertices_gpu_va + debugcam_frustum.vertices_byte_offset;
        main_cmdlist->IASetVertexBuffers(0, 1, &debug_vbv);

        D3D12_INDEX_BUFFER_VIEW debug_ibv;
        debug_ibv.BufferLocation = indices_gpu_va + debugcam_frustum.indices_byte_offset;
        debug_ibv.SizeInBytes = debugcam_frustum.indices_byte_size;
        debug_ibv.Format = DXGI_FORMAT_R16_UINT;
        main_cmdlist->IASetIndexBuffer(&debug_ibv);

        main_cmdlist->DrawIndexedInstanced(debugcam_frustum.indices_count, 1, 0, 0, 0);

        // Main cam
        debug_vbv.SizeInBytes = maincam_frustum.vertices_byte_size;
        debug_vbv.StrideInBytes = sizeof(position_color);
        debug_vbv.BufferLocation = vertices_gpu_va + maincam_frustum.vertices_byte_offset;
        main_cmdlist->IASetVertexBuffers(0, 1, &debug_vbv);

        debug_ibv.BufferLocation = indices_gpu_va + maincam_frustum.indices_byte_offset;
        debug_ibv.SizeInBytes = maincam_frustum.indices_byte_size;
        debug_ibv.Format = DXGI_FORMAT_R16_UINT;
        main_cmdlist->IASetIndexBuffer(&debug_ibv);

        main_cmdlist->DrawIndexedInstanced(maincam_frustum.indices_count, 1, 0, 0, 0);

        // Draw debug camera planes
        main_cmdlist->SetPipelineState(debug_plane_pso);
        main_cmdlist->IASetPrimitiveTopology(D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        D3D12_VERTEX_BUFFER_VIEW debug_planes_vbv;
        debug_planes_vbv.BufferLocation = vertices_gpu_va + debugcam_frustum_planes.vertices_byte_offset;
        debug_planes_vbv.StrideInBytes = sizeof(position_color);
        debug_planes_vbv.SizeInBytes = debugcam_frustum_planes.vertices_byte_size;
        main_cmdlist->IASetVertexBuffers(0, 1, &debug_planes_vbv);

        D3D12_INDEX_BUFFER_VIEW debug_planes_ibv;
        debug_planes_ibv.BufferLocation = indices_gpu_va + debugcam_frustum_planes.indices_byte_offset;
        debug_planes_ibv.SizeInBytes = debugcam_frustum_planes.indices_byte_size;
        debug_planes_ibv.Format = DXGI_FORMAT_R16_UINT;
        main_cmdlist->IASetIndexBuffer(&debug_planes_ibv);

        main_cmdlist->DrawIndexedInstanced(debugcam_frustum_planes.indices_count, 1, 0, 0, 0);
    }

    auto res = dr->readback<simulation_indirect_command>(input_simcmds_grouped_default, num_particle_systems_at_launch);
    accum_1 = res[0].dt_accum;
    accum_2 = res[1].dt_accum;
    missed_frames_1 = res[0].missed_frames;
    missed_frames_2 = res[1].missed_frames;

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

    ImGui::InputFloat("dt accum 1", &accum_1, 1.f, 1.2f, 3);
    ImGui::InputFloat("dt accum 2", &accum_2, 1.f, 1.2f, 3);

    ImGui::InputInt("missed frames 1", &missed_frames_1);
    ImGui::InputInt("missed frames 2", &missed_frames_2);

    if (!is_capturing_frames)
    {
        if (ImGui::Button("Begin capture"))
        {
            dr->begin_capture();
            is_capturing_frames = true;
        }
    }
    else
    {
        if (ImGui::Button("End capture"))
        {
            dr->end_capture();
            is_capturing_frames = false;
        }
    }

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

    //ImGui::Text("Particles alive: %d / %d", particle_system->m_num_particles_alive, particle_system->m_max_particles_per_frame);
    //ImGui::Text("Particles total: %d", particle_system->m_num_particles_total);

    ImGui::Separator();
    ImGui::Checkbox("Show camera debug planes", &is_camera_debug_info_visible);
    imgui_combobox((int *)&current_camera, {"Main", "Debug"}, "Current camera");
    ImGui::Separator();

    if (ImGui::Button("Reset particle system"))
    {
        // Only need to set the reset_cmdlist once
        if (is_first_reset)
        {
            size_t sim_offset = 0;
            size_t draw_offset = 0;
            size_t bounds_draw_offset = 0;
            size_t bounds_calc_offset = 0;
            size_t indirect_sim_size = sizeof(simulation_indirect_command);
            size_t indirect_draw_size = sizeof(draw_indirect_command2);
            size_t indirect_bounds_draw_size = sizeof(bounds_draw_indirect_command);
            size_t indirect_bounds_calc_size = sizeof(bounds_calc_indirect_command);

            for (particle_system_gpu &particle_system : particle_systems)
            {
                particle_system.reset(reset_cmdlist);

                // Reset simulation commands
                reset_cmdlist->CopyBufferRegion(input_simcmds_grouped_default, sim_offset,
                                               particle_system.m_indirect_sim_cmds_input_default, 0,
                                               indirect_sim_size);
                reset_cmdlist->CopyBufferRegion(swap_simcmds_grouped_default, sim_offset,
                                               particle_system.m_indirect_sim_cmds_swap_default, 0,
                                               indirect_sim_size);
                sim_offset += indirect_sim_size;

                //// Reset draw commands
                //reset_cmdlist->CopyBufferRegion(input_drawcmds_grouped_default, draw_offset,
                //                               particle_system.m_indirect_drawing_default, 0,
                //                               indirect_draw_size);
                //draw_offset += indirect_draw_size;

                //// Reset bounds drawing commands
                //reset_cmdlist->CopyBufferRegion(input_bounds_drawcmds_grouped_default, bounds_draw_offset,
                //                               particle_system.m_indirect_drawing_bounds_default, 0,
                //                               indirect_bounds_draw_size);
                //bounds_draw_offset += indirect_bounds_draw_size;

                //// Reset bounds calculation commands
                //reset_cmdlist->CopyBufferRegion(input_bounds_calccmds_grouped_default, bounds_calc_offset,
                //                               particle_system.m_indirect_bounds_calc_default, 0,
                //                               indirect_bounds_calc_size);
                //bounds_calc_offset += indirect_bounds_calc_size;
            }

            reset_cmdlist->Close();

            is_first_reset = false;
        }

        // Wait for the graphics queue to finish reading the resources before we reset them
        dr->debug_copy_queue->Wait(dr->fence, dr->last_signaled_fence_value);
        dr->debug_copy_queue->ExecuteCommandLists(1, (ID3D12CommandList *const *)&reset_cmdlist);
        dr->debug_copy_queue->Signal(dr->copy_fence, ++dr->copy_fence_value);

        // Right now, this is preventing the graphics queue from reading too early,
        // because it's on the same thread as the copy queue
        dr->copy_fence->SetEventOnCompletion(dr->copy_fence_value, dr->copy_fence_event);
        WaitForSingleObject(dr->copy_fence_event, INFINITE);

        //for (size_t i = 0; i < _countof(frame_resources); i++)
        //{
        //    particle_system->reset(reinterpret_cast<particle::particle>(frame_resources[i]->particle_vb_range));
        //}
    }

    ImGui::Separator();
    ImGui::Text("Physics parameters");
    ImGui::DragFloat2("Drag coefficients", cb_physics.drag_coefficients, 0.25f, 0.f, 50.f);
    ImGui::Separator();

    //imgui_combobox((int *)&particle_system->m_rendering_mode, {"Point", "Billboard", "Overdraw"}, "Particle rendering mode");
    //imgui_combobox((int *)&particle_system->m_simulation_mode, {"CPU", "GPU"}, "Particle simulation mode");
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

    // Bounding box calculator
    compile_shader(L"..\\..\\particles\\shaders\\calculate_bounds.hlsl", L"CS", shader_type::compute, &calc_bounds_blob_cs);

    // Debug visualization shaders
    compile_shader(L"..\\..\\particles\\shaders\\debug.hlsl", L"VS", shader_type::vertex, &debug_lines_blob_vs);
    compile_shader(L"..\\..\\particles\\shaders\\debug.hlsl", L"PS", shader_type::pixel, &debug_lines_blob_ps);

    compile_shader(L"..\\..\\particles\\shaders\\debug_plane.hlsl", L"VS", shader_type::vertex, &debug_planes_blob_vs);
    compile_shader(L"..\\..\\particles\\shaders\\debug_plane.hlsl", L"PS", shader_type::pixel, &debug_planes_blob_ps);

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

    // (table) StructuredBuffer<sim_indirect_command> : register(u8);
    CD3DX12_ROOT_PARAMETER1 param_in_commands = {};
    param_in_commands.InitAsUnorderedAccessView(8, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE);
    params.push_back(param_in_commands);

    //// (table) StructuredBuffer<sim_indirect_command> : register(t1);
    //CD3DX12_ROOT_PARAMETER1 param_in_commands = {};
    //param_in_commands.InitAsShaderResourceView(1, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    //params.push_back(param_in_commands);

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

    // (table) AppendStructuredBuffer<bounding_box> : register(u3);
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

    // (root) StructuredBuffer<bounds_draw_indirect_command> in_bounds_draw_commands : register(t3);
    CD3DX12_ROOT_PARAMETER1 param_input_bounds_drawcmds = {};
    param_input_bounds_drawcmds.InitAsShaderResourceView(3, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    params.push_back(param_input_bounds_drawcmds);

    // (root) StructuredBuffer<bounding_box> inputs : register(u6);
    CD3DX12_ROOT_PARAMETER1 param_bounds_read = {};
    param_bounds_read.InitAsShaderResourceView(5, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_VOLATILE);
    params.push_back(param_bounds_read);

    // (root) StructuredBuffer<bounds_calc_indirect_command> in_bounds_calc_commands : register(t4);
    CD3DX12_ROOT_PARAMETER1 param_input_bounds_calccmds = {};
    param_input_bounds_calccmds.InitAsShaderResourceView(4, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    params.push_back(param_input_bounds_calccmds);

    // AppendStructuredBuffer<bounds_calc_indirect_command> out_bounds_calc_commands : register(u7);
    CD3DX12_DESCRIPTOR_RANGE1 calc_cmd_ranges[1];
    calc_cmd_ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 7, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);
    CD3DX12_ROOT_PARAMETER1 param_calc_cmds = {};
    param_calc_cmds.InitAsDescriptorTable(_countof(calc_cmd_ranges), calc_cmd_ranges);
    params.push_back(param_calc_cmds);

    // (root) ConstantBuffer<physics> cb_physics : register(b4);
    CD3DX12_ROOT_PARAMETER1 physics_param = {};
    physics_param.InitAsConstantBufferView(4, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC);
    params.push_back(physics_param);

    //ConstantBuffer<per_particle_system_constants> cb_ps_data : register(b5);
    CD3DX12_ROOT_PARAMETER1 dt_param = {};
    dt_param.InitAsConstants(2, 5);
    params.push_back(dt_param);

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

    // Debug lines PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC debug_lines_pso_desc = dr->create_default_pso_desc(&mesh_input_elem_desc);
    debug_lines_pso_desc.VS = {debug_lines_blob_vs->GetBufferPointer(), debug_lines_blob_vs->GetBufferSize()};
    debug_lines_pso_desc.PS = {debug_lines_blob_ps->GetBufferPointer(), debug_lines_blob_ps->GetBufferSize()};
    debug_lines_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    debug_lines_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    check_hr(device->CreateGraphicsPipelineState(&debug_lines_pso_desc, IID_PPV_ARGS(&debug_line_pso)));
    NAME_D3D12_OBJECT(debug_line_pso);

    // Debug planes PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC debug_planes_pso_desc = dr->create_default_pso_desc(&mesh_input_elem_desc);
    debug_planes_pso_desc.VS = {debug_planes_blob_vs->GetBufferPointer(), debug_planes_blob_vs->GetBufferSize()};
    debug_planes_pso_desc.PS = {debug_planes_blob_ps->GetBufferPointer(), debug_planes_blob_ps->GetBufferSize()};
    debug_planes_pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    debug_planes_pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    debug_planes_pso_desc.BlendState = transparency_blend_desc;
    check_hr(device->CreateGraphicsPipelineState(&debug_planes_pso_desc, IID_PPV_ARGS(&debug_plane_pso)));
    NAME_D3D12_OBJECT(debug_plane_pso);

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

    // Bounds calculator pso
    D3D12_COMPUTE_PIPELINE_STATE_DESC calc_bounds_pso_desc = particle_sim_pso_desc;
    calc_bounds_pso_desc.CS = {calc_bounds_blob_cs->GetBufferPointer(), calc_bounds_blob_cs->GetBufferSize()};
    check_hr(device->CreateComputePipelineState(&calc_bounds_pso_desc, IID_PPV_ARGS(&calc_bounds_pso)));
    NAME_D3D12_OBJECT(calc_bounds_pso);
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
    srv_heap_desc.NumDescriptors = 8;
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
    main_cam->calc_projection();
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
    safe_release(main_cmdlist);
    safe_release(fire_texture_default_resource);
    safe_release(fire_texture_upload_resource);
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
                switch (current_camera)
                {
                case main:
                    main_cam->update_yaw_pitch(XMFLOAT2(ImGui::GetMousePos().x, ImGui::GetMousePos().y),
                                               XMFLOAT2(last_mouse_pos.x, last_mouse_pos.y));
                    break;
                case debug:
                    debug_cam->update_yaw_pitch(XMFLOAT2(ImGui::GetMousePos().x, ImGui::GetMousePos().y),
                                                XMFLOAT2(last_mouse_pos.x, last_mouse_pos.y));
                    break;
                default:
                    break;
                }
            }
        }

        last_mouse_pos = ImGui::GetMousePos();
        break;
    }
}