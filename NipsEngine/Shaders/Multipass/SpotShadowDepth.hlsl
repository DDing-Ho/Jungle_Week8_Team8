cbuffer SpotShadowBuffer : register(b0)
{
    row_major float4x4 LightViewProj;
    float ShadowResolution;
    float ShadowBias;
    float2 SpotShadowPadding;
};

cbuffer PerObjectBuffer : register(b1)
{
    row_major float4x4 World;
};

struct FSpotShadowVSInput
{
    float3 Position : POSITION;
    float4 Color : COLOR;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
    float3 Tangent : TANGENT;
    float3 Bitangent : BITANGENT;
};

struct FSpotShadowVSOutput
{
    float4 ClipPos : SV_POSITION;
};

FSpotShadowVSOutput mainVS(FSpotShadowVSInput Input)
{
    FSpotShadowVSOutput Output;

    const float4 WorldPos = mul(float4(Input.Position, 1.0f), World);
    Output.ClipPos = mul(WorldPos, LightViewProj);

    return Output;
}

void mainPS(FSpotShadowVSOutput Input)
{
}