#include "pch.h"
#include "mcallister_system.h"
#include <algorithm>
#include <random>
#include "../PerformanceAPI_capi.h"

namespace particle
{

// System
mcallister_system::mcallister_system(source *src, std::vector<action *> actions, ID3D12Device *device)
{
    m_source.reset(src);

    for (action *act : actions)
        m_actions.emplace_back(act);

    // allocate sizeof AOS (32 bytes) * number of particles (1000) =  32000 bytes
    // Multiplies 32000 by the frame_count inside the function to reach 128000 bytes total
    m_vertexbuffer_stride = m_max_particles_per_frame * size;
    m_num_particles_total = m_max_particles_per_frame * NUM_BACK_BUFFERS;

    m_vertex_upload_resource = new upload_buffer(device, m_num_particles_total, size, "particles_vertices");
}

mcallister_system::~mcallister_system()
{
    delete m_vertex_upload_resource;
}

void mcallister_system::reset(particle ptr)
{
    ptr->age = 0.f;
    ptr->color = XMCOLOR(0.f, 0.f, 0.f, 0.f);
    ptr->position = XMFLOAT3(0.f, 0.f, 0.f);
    ptr->velocity = XMFLOAT3(0.f, 0.f, 0.f);
    m_num_particles_alive = 0;
}

std::pair<particle, particle> mcallister_system::simulate(float dt, particle current_partition)
{
    PerformanceAPI_BeginEvent("simulation", nullptr, PERFORMANCEAPI_DEFAULT_COLOR);

    particle partition_start = current_partition;
    particle partition_ptr = current_partition + m_num_particles_alive;
    particle partition_end = current_partition + m_max_particles_per_frame;

    for (size_t i = 0; i < m_num_particles_alive; i++)
    {
        partition_start[i] = m_previous_particle[i];

        // Run actions
        for (auto &act : m_actions)
        {
            act.get()->apply(dt, partition_start + i);
        }
    }

    // Spawn new particles
    partition_ptr = m_source->apply(dt, partition_ptr, partition_end);

    m_num_particles_alive = partition_ptr - partition_start;
    m_previous_particle = current_partition;

    // parition the pool, reconcile the emitted with the timed-out
    partition_ptr = std::partition(partition_start, partition_ptr, [](auto &v) { return v.age <= 5; });

    PerformanceAPI_EndEvent();

    return std::make_pair(partition_start, partition_ptr);
}

BYTE *mcallister_system::get_frame_partition(int frame_index)
{
    BYTE *ptr = m_vertex_upload_resource->m_mapped_data;
    return ptr + (m_vertexbuffer_stride * frame_index);
}

// Sources
flow::flow(double particles_per_second, std::vector<initializer *> initializers)
{
    m_particles_per_second = particles_per_second;

    for (initializer *initializer : initializers)
        m_initializers.emplace_back(initializer);
}

flow::~flow()
{
}

particle flow::apply(float dt, particle begin, particle end)
{
    m_time += dt;
    // Calculate the number of particles we should have at this time
    size_t num_particles_to_create = size_t(m_particles_per_second * m_time);

    bool should_spawn_particles = num_particles_to_create > m_num_created;
    if (should_spawn_particles)
    {
        // Calculate how many particles can be created
        size_t remaining_particle_slots = size_t(end - begin);
        num_particles_to_create = std::min(num_particles_to_create - m_num_created, remaining_particle_slots);

        // Initialize the new particles
        for (size_t i = 0; i < num_particles_to_create; ++i)
        {
            for (auto &initializer : m_initializers)
            {
                initializer->apply(dt, begin + i);
            }
        }
        m_num_created += num_particles_to_create;

        // Return the end of the created particles
        return begin + num_particles_to_create;
    }
    else
    {
        return begin;
    }
}

// Initializers
point::point(XMFLOAT3 v)
{
    m_point = v;
}

void point::emit(XMFLOAT3 &v)
{
    v = m_point;
}

constant::constant(float v)
{
    m_constant = v;
}

void constant::emit(float &v)
{
    v = m_constant;
}

random::random(float first, float second)
{
    m_first = first;
    m_second = second;
}

void random::emit(float &v)
{
    std::random_device rd;                                     // obtain a random number from hardware
    std::mt19937 gen(rd());                                    // seed the generator
    std::uniform_real_distribution<> distr(m_first, m_second); // define the range
    v = (float)distr(gen);
}

cylinder::cylinder(XMVECTOR const &pt1, XMVECTOR const &pt2, float rd1, float rd2)
{
    // A cyclinder is represented by:
    // - a point
    // - a vector to the other end point
    // - two radi
    m_p1 = pt1;                 // first point
    m_p2 = pt2 - pt1;           // vector from first point to the other end point
    m_rd1 = std::max(rd1, rd2); // Largest radius
    m_rd2 = std::min(rd1, rd2); // Smallest radius

    XMVECTOR norm_p2 = XMVector3Normalize(m_p2);

    // Calculate two basis vectors of the plane containing the point
    XMVECTOR basis = XMVectorSet(1.f, 0.f, 0.f, 0.f);

    XMVECTOR bn_dot = XMVector4Dot(basis, norm_p2);
    XMVECTOR res = basis - (norm_p2 * bn_dot);
    m_u = XMVector3Normalize(res);
    m_v = XMVector3Cross(norm_p2, m_u);
}

void cylinder::emit(XMFLOAT3 &v)
{
    // Random number between 0 and 2PI
    std::random_device rd;                               // obtain a random number from hardware
    std::mt19937 gen(rd());                              // seed the generator
    std::uniform_real_distribution<> distr(0.0, XM_2PI); // define the range
    float random_float = (float)distr(gen);

    //random_float = 5.81135607f;
    // Create a vector that represent a random position on a unit circle's edge
    XMVECTOR random_circle_pos = XMVectorSet(std::cosf(random_float), std::sinf(random_float), 0.f, 0.f);

    // Scale it by the given radius
    XMVECTOR scaled_circle_pos = XMVectorScale(random_circle_pos, m_rd1);

    // Pick a random point on the disc
    std::uniform_real_distribution<> distr2(0.0, 1.0);
    float random_float2 = (float)distr2(gen);
    //random_float2 = 0.128962114f;
    XMVECTOR random_point = m_p1 + (m_p2 * random_float2);
    random_point = random_point + XMVectorScale(m_u, XMVectorGetX(scaled_circle_pos)) + XMVectorScale(m_v, XMVectorGetY(scaled_circle_pos));
    XMStoreFloat3(&v, random_point);
}

// Actions
void move::apply(float dt, particle particle)
{
    XMVECTOR vdt = XMVectorSet(dt, dt, dt, dt);
    XMVECTOR pos = XMLoadFloat3(&particle->position);
    XMVECTOR vel = XMLoadFloat3(&particle->velocity);
    pos = XMVectorMultiplyAdd(vel, vdt, pos);
    particle->age += dt;
    XMStoreFloat3(&particle->position, pos);
}

gravity::gravity(XMVECTOR const &v)
{
    m_g = v;
}

void gravity::apply(float dt, particle particle)
{
    XMVECTOR vdt = XMVectorSet(dt, dt, dt, dt);
    XMVECTOR vel = XMLoadFloat3(&particle->velocity);
    vel = XMVectorMultiplyAdd(m_g, vdt, vel);
    XMStoreFloat3(&particle->velocity, vel);
}

} // namespace particle
