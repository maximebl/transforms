#pragma once
#include "common.h"
#include "pch.h"

// Still put the correct calling convention for vector types (float, double, __m128,__m256, __m512) in case the compiler doesn't inline the function.

COMMON_API inline DirectX::XMMATRIX __vectorcall view_matrix_lh(DirectX::FXMVECTOR camera_pos,
                                                                DirectX::FXMVECTOR world_up_dir,
                                                                DirectX::FXMVECTOR target_pos);

COMMON_API DirectX::XMFLOAT4X4 Identity4x4();
COMMON_API DirectX::XMFLOAT3X3 Identity3x3();
COMMON_API float random_float(float first, float second);
