struct pass_data
{
    matrix view;
    matrix proj;
    float3 eye_pos;
    float time;
};
ConstantBuffer<pass_data> cb_pass : register(b0);

// input layout
struct vertex_in
{
    float3 position : POSITION;
    float size : SIZE;
    float3 velocity : VELOCITY;
    float age : AGE;
};
