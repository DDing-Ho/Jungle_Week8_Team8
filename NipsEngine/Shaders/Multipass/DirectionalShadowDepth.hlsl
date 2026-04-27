cbuffer DirectionalShadowBuffer : register(b0)
{
    row_major float4x4 LightViewProj;
};

cbuffer PerObjectBuffer : register(b1)
{
    row_major float4x4 World;
};

struct FDirectionalShadowVSInput
{
    float3 Position : POSITION;
    float4 Color : COLOR;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
};

struct FDirectionalShadowVSOutput
{
    float4 ClipPos : SV_POSITION;
};

FDirectionalShadowVSOutput mainVS(FDirectionalShadowVSInput Input)
{
    FDirectionalShadowVSOutput Output;
    float4 WorldPos = mul(float4(Input.Position, 1.0f), World);
    Output.ClipPos = mul(WorldPos, LightViewProj);
    
    return Output;
}

void mainPS(FDirectionalShadowVSOutput Input)
{
}
