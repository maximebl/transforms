#pragma once
#include <DirectXMath.h>

namespace particle
{
using namespace DirectX;

//Allow for 2 particles per cache line.
s_internal constexpr int half_cache_line = XM_CACHE_LINE_SIZE / 2;

struct alignas(half_cache_line) aligned_aos
{
    XMFLOAT3 position; // 12 bytes
    float size;        // 4 bytes -- 16 bytes alignment
    XMFLOAT3 velocity; // 12 bytes
    float age;         // 4 bytes -- 16 bytes alignment
};
static_assert(std::alignment_of<aligned_aos>::value == half_cache_line, "aligned_particle_aos must be 32 bytes aligned.");
s_internal constexpr size_t byte_size = sizeof(aligned_aos);
static_assert(byte_size == half_cache_line, "aligned_particle_aos must be 32 bytes wide.");

} // namespace particle
