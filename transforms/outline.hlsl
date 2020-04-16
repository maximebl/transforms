//IA
struct VertexPosColor
{
    float2 Position : POSITION;
    float3 Color : COLOR;
};

struct VertexShaderOutput
{
    float4 Color : COLOR;
    float4 Position : SV_Position;
};

//Bindings

struct model
{
    matrix model;
};
ConstantBuffer<model> cb_model : register(b1);

VertexShaderOutput VS(VertexPosColor IN, uint instanceID : SV_InstanceID)
{
    VertexShaderOutput OUT;

    float4 Hpos = float4((IN.Position.xy * (instanceID + 1)) * 1.1f, 0.0f, 1.0f);
    OUT.Position = mul(Hpos, cb_model.model);
    OUT.Color = float4(IN.Color, 1.f);
    return OUT;
}

struct PixelShaderInput
{
    float4 Color : COLOR;
};

float4 PS(PixelShaderInput IN) : SV_Target
{
    return float4(0.16, 0.29f, 0.48, 1.f);
}
