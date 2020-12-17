#include "pch.h"
#include "math_helpers.h"
#include <random>

using namespace DirectX;

XMMATRIX __vectorcall view_matrix_lh(FXMVECTOR camera_pos,
                                     FXMVECTOR world_up_dir,
                                     FXMVECTOR target_pos)
{
    XMVECTOR camera_dir = XMVector3Normalize(XMVectorSubtract(target_pos, camera_pos));
    XMVECTOR camera_right = XMVector3Normalize(XMVector3Cross(world_up_dir, camera_dir));
    XMVECTOR camera_up = XMVector3Cross(camera_dir, camera_right);
    camera_up.m128_f32[3] = 0.f;
    camera_dir.m128_f32[3] = 0.f;
    camera_right.m128_f32[3] = 0.f;

    XMVECTOR neg_cam_pos = XMVectorNegate(camera_pos);
    XMMATRIX T;
    T.r[0] = camera_right;
    T.r[1] = camera_up;
    T.r[2] = camera_dir;
    T.r[3].m128_f32[0] = XMVectorGetX(XMVector3Dot(camera_right, neg_cam_pos));
    T.r[3].m128_f32[1] = XMVectorGetX(XMVector3Dot(camera_up, neg_cam_pos));
    T.r[3].m128_f32[2] = XMVectorGetX(XMVector3Dot(camera_dir, neg_cam_pos));
    T.r[3].m128_f32[3] = 1.f;
    return T;
}

DirectX::XMFLOAT4X4 Identity4x4()
{
    DirectX::XMFLOAT4X4 I(
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f);

    return I;
}

DirectX::XMFLOAT3X3 Identity3x3()
{
    DirectX::XMFLOAT3X3 I(
        1.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 1.0f);

    return I;
}

float random_float(float min, float max)
{
    static std::random_device rd{};
    static std::mt19937 gen{rd()};

    std::uniform_real_distribution<float> distr{min, max};
    return distr(gen);
}

float plane_dot(XMVECTOR plane, XMVECTOR point)
{
    return XMVectorGetX(XMVector3Dot(plane, point)) + XMVectorGetW(plane);
}

bool is_aabb_visible(XMVECTOR planes[6], XMVECTOR center, XMVECTOR extents)
{
    for (size_t i = 0; i < 6; i++)
    {
        XMVECTOR plane = planes[i];
        float aabb_radius = fabs(XMVectorGetX(plane) * XMVectorGetX(extents)) + fabs(XMVectorGetY(plane) * XMVectorGetY(extents)) + fabs(XMVectorGetZ(plane) * XMVectorGetZ(extents));
        float to_center = plane_dot(plane, center);
        float neg_rg = -aabb_radius;
        if (to_center <= -aabb_radius)
            return false;
    }
    return true;
}
