#pragma once
#include "common.h"
#include <d3d12.h>
#include "directxpackedvector.h"
#include <memory>
#include <gpu_interface.h>

namespace particle
{
using namespace DirectX;
using namespace DirectX::PackedVector;

// Allow for 2 particles per cache line.
s_internal constexpr int half_cache_line = XM_CACHE_LINE_SIZE / 2;

struct alignas(half_cache_line) aligned_particle_aos
{
    XMFLOAT3 position; // 12 bytes
    XMCOLOR color;     // 4 bytes -- 16 bytes alignment
    XMFLOAT3 velocity; // 12 bytes
    float age;         // 4 bytes -- 16 bytes alignment
};

s_internal constexpr size_t size = sizeof(aligned_particle_aos);

static_assert(std::alignment_of<aligned_particle_aos>::value == half_cache_line, "aligned_particle_aos must be 32 bytes aligned.");
static_assert(size == half_cache_line, "aligned_particle_aos must be 32 bytes wide.");

using particle = aligned_particle_aos *__restrict;

struct source
{
    virtual particle apply(float dt, particle begin, particle end) = 0;
    virtual ~source() {}
};

struct initializer
{
    virtual void apply(float, particle) = 0;
    virtual ~initializer() {}
};

struct action
{
    virtual void apply(float, particle) = 0;
    virtual ~action() {}
};

// Domains
struct point
{
    XMFLOAT3 m_point;
    point(XMFLOAT3 v);
    void emit(XMFLOAT3 &v);
};

struct cylinder
{
    XMVECTOR m_p1, m_p2, m_u, m_v;
    float m_rd1, m_rd2;
    cylinder(XMVECTOR const &p1, XMVECTOR const &p2, float r1, float r2);
    void emit(XMFLOAT3 &v);
};

struct constant
{
    float m_constant;
    constant(float v);
    void emit(float &v);
};

// Initializers
template <typename domain>
struct position : initializer
{
    domain m_domain;
    position(domain new_domain) : m_domain(new_domain){};
    void apply(float dt, particle p) override { m_domain.emit(p->position); };
};

template <typename domain>
struct velocity : initializer
{
    domain m_domain;
    velocity(domain new_domain) : m_domain(new_domain){};
    void apply(float dt, particle p) override { m_domain.emit(p->velocity); };
};

template <typename domain>
struct age : initializer
{
    domain m_domain;
    age(domain new_domain) : m_domain(new_domain){};
    void apply(float dt, particle p) override { m_domain.emit(p->age); };
};

// Sources
struct flow : source
{
    flow(double particles_per_second, std::vector<initializer *> initializers);
    virtual ~flow();
    particle apply(float dt, particle begin, particle end) override;

    float m_time = 0;
    size_t m_num_created = 0;
    double m_particles_per_second = 0.0;
    std::vector<std::unique_ptr<initializer>> m_initializers = {};
};

// Actions
struct move : action
{
    void apply(float dt, particle particle) override;
};

struct gravity : action
{
    gravity(XMVECTOR const& v);
    XMVECTOR m_g;
    void apply(float dt, particle particle) override;
};

struct mcallister_system
{
    mcallister_system(source *src, std::vector<action *> actions, ID3D12Device *device);
    ~mcallister_system();
    std::pair<particle, particle> simulate(float dt, particle current_particle);
    BYTE *get_frame_partition(int frame_index);
    upload_buffer *m_vertex_upload_resource = nullptr;
    size_t m_vertexbuffer_stride = 0;
    size_t m_num_particles_total = 0;
    size_t m_num_particles_alive = 0;
    static constexpr int m_max_particles = 1000;

private:
    std::vector<std::unique_ptr<action>> m_actions = {};
    particle m_particle = nullptr;
    std::unique_ptr<source> m_source = nullptr;
    particle m_current_particle = nullptr;
    particle m_previous_particle = nullptr;
};

} // namespace particle
