//IA
struct VertexPosColor
{
    float4 Position : POSITION;
    float4 Color : COLOR;
};

struct VertexShaderOutput
{
    float4 Color : COLOR;
    float4 Position : SV_Position;
};

//Bindings

// Method 1
struct model_to_proj
{
    matrix model_to_projection;
};
ConstantBuffer<model_to_proj> cb_modelproj : register(b0);

// Method 2
struct model
{
    matrix model;
};
ConstantBuffer<model> cb_model : register(b1);

struct view_and_proj
{
    matrix view;
    matrix projection;
};
ConstantBuffer<view_and_proj> cb_viewproj : register(b2);

VertexShaderOutput VS(VertexPosColor IN)
{
    VertexShaderOutput OUT;

    OUT.Position = mul(IN.Position, cb_model.model);
    OUT.Color = IN.Color;
    return OUT;
}

struct PixelShaderInput
{
    float4 Color : COLOR;
};

float4 PS(PixelShaderInput IN) : SV_Target
{
    return IN.Color;
}
