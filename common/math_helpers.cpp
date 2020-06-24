#include "pch.h"
#include "math_helpers.h"

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