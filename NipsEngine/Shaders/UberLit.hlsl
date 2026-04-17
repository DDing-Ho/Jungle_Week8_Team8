#include "Util.hlsli"

#define NUM_POINT_LIGHT 4
#define NUM_SPOT_LIGHT 4
#define NUM_DECAL 32

struct FAmbientLightInfo
{
	// TODO : 정렬 맞추십쇼
};

struct FDirectionalLightInfo
{
	// TODO : 정렬 맞추십쇼
};

struct FPointLightInfo
{
	// TODO : 정렬 맞추십쇼
};

struct FSpotLightInfo
{
	// TODO : 정렬 맞추십쇼
};

cbuffer FrameBuffer : register(b0)
{
    row_major float4x4 View;
    row_major float4x4 Projection;
    float3 CameraPosition;
    float Padding1;
}

cbuffer Lighting : register(b1)
{
    FAmbientLightInfo Ambient;
    FDirectionalLightInfo Directional;
    FPointLightInfo PointLights[NUM_POINT_LIGHT];
    FSpotLightInfo SpotLights[NUM_SPOT_LIGHT];
}

cbuffer PerObject : register(b2)
{
    row_major float4x4 World;
    row_major float4x4 WorldInvTans;
}

Texture2D DiffuseMap : register(t0);
Texture2D NormalMap : register(t1);

SamplerState SampleState : register(s0);

struct VSInput
{
    float3 Position : POSITION;
    float4 Color : COLOR;
    float3 Normal : NORMAL;
    float2 UV : TEXCOORD;
};

struct PSInput
{
    float4 ClipPos : SV_POSITION;
    float3 WorldPos : TEXCOORD0;
    float3 WorldNormal : TEXCOORD1;
    float2 UV : TEXCOORD2;
#if LIGHTING_MODEL_GOURAUD
    float4 LightColor : TEXCOORD3;
#endif
};

PSInput Uber_VS(VSInput Input)
{
    PSInput output;

#if LIGHTING_MODEL_GOURAUD

#elif LIGHTING_MODEL_LAMBERT

#elif LIGHTING_MODEL_PHONG

#endif

    return output;
}

struct PSOutput
{
    float4 Color : SV_TARGET0;
    float4 Normal : SV_TARGET1;
    float4 WorldPos : SV_TARGET2;
};

PSOutput Uber_PS(PSInput Input)
{
    PSOutput output;

    // Primitive

    // Decal

    // Lighting
#if LIGHTING_MODEL_GOURAUD

#elif LIGHTING_MODEL_LAMBERT

#elif LIGHTING_MODEL_PHONG

#endif

    return output;
}
