cbuffer PointShadowBuffer : register(b0)
{
    row_major float4x4 LightViewProj;
    float3 LightPosition;
    float  FarPlane;
    float  ShadowBias;
    float  ShadowResolution;
    float2 PointShadowPadding;
};

cbuffer PerObjectBuffer : register(b1)
{
    row_major float4x4 World;
};

struct FPointShadowVSInput
{
    float3 Position  : POSITION;
    float4 Color     : COLOR;
    float3 Normal    : NORMAL;
    float2 UV        : TEXCOORD;
    float3 Tangent   : TANGENT;
    float3 Bitangent : BITANGENT;
};

struct FPointShadowVSOutput
{
    float4 ClipPos  : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
};

FPointShadowVSOutput mainVS(FPointShadowVSInput Input)
{
    FPointShadowVSOutput Output;

    float4 WorldPos4 = mul(float4(Input.Position, 1.0f), World);
    Output.WorldPos  = WorldPos4.xyz;
    Output.ClipPos   = mul(WorldPos4, LightViewProj);

    return Output;
}

void mainPS(FPointShadowVSOutput Input, out float OutDepth : SV_Depth)
{
    float Dist = length(Input.WorldPos - LightPosition);
    OutDepth = saturate(Dist / FarPlane);
}
