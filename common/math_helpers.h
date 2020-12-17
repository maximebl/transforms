#pragma once
#include "common.h"
#include "pch.h"

// Still put the correct calling convention for vector types (float, double, __m128,__m256, __m512) in case the compiler doesn't inline the function.

COMMON_API inline DirectX::XMMATRIX __vectorcall view_matrix_lh(DirectX::FXMVECTOR camera_pos,
                                                                DirectX::FXMVECTOR world_up_dir,
                                                                DirectX::FXMVECTOR target_pos);

COMMON_API DirectX::XMFLOAT4X4 Identity4x4();
COMMON_API DirectX::XMFLOAT3X3 Identity3x3();
COMMON_API float random_float(float min, float max);
COMMON_API float plane_dot(DirectX::XMVECTOR plane, DirectX::XMVECTOR point);
COMMON_API bool is_aabb_visible(DirectX::XMVECTOR planes[6], DirectX::XMVECTOR center, DirectX::XMVECTOR extents);
