#include "UberSurface.hlsli"
#include "ShadowDepth.hlsl"

#if !defined(MATERIAL_DOMAIN_DECAL) && !defined(LIGHTING_MODEL_GOURAUD) && !defined(LIGHTING_MODEL_LAMBERT) && !defined(LIGHTING_MODEL_PHONG) && !defined(LIGHTING_MODEL_TOON)
#define LIGHTING_MODEL_PHONG 1
#endif

cbuffer UberLighting : register(b3)
{
    uint SceneGlobalLightCount;
    float3 _UberLightingPad0;
}

struct FGPULight
{
    uint Type;
    float Intensity;
    float Radius;
    float FalloffExponent;

    float3 Color;
    float SpotInnerCos;

    float3 Position;
    float SpotOuterCos;

    float3 Direction;
    float Padding0;
    
    row_major float4x4 ShadowLightViewProj;
};

StructuredBuffer<FGPULight> GlobalLights : register(t3);

cbuffer VisibleLightInfo : register(b4)
{
    uint TileCountX;
    uint TileCountY;
    uint TileSize;
    uint MaxLightsPerTile;
    uint VisibleLightCount;
    float3 _VisibleLightInfoPad0;
}

struct FVisibleLightData
{
    row_major float4x4 ShadowLightViewProj;
    float3 WorldPos;
    float Radius;
    float3 Color;
    float Intensity;
    float RadiusFalloff;
    uint Type;
    float SpotInnerCos;
    float SpotOuterCos;
    float3 Direction;
    float _Pad;
};

StructuredBuffer<FVisibleLightData> VisibleLights : register(t8);
StructuredBuffer<uint> TileVisibleLightCount : register(t9);
StructuredBuffer<uint> TileVisibleLightIndices : register(t10);
Texture2D ShadowMap : register(t11);

static const uint LIGHT_TYPE_DIRECTIONAL = 0u;
static const uint LIGHT_TYPE_POINT = 1u;
static const uint LIGHT_TYPE_SPOT = 2u;
static const uint LIGHT_TYPE_AMBIENT = 3u;
static const float3 DEFAULT_AMBIENT_COLOR = float3(0.02f, 0.02f, 0.02f);

struct FLightingResult
{
    float3 Diffuse;
    float3 Specular;
};

float SampleShadow(float4 worldPos)
{
    float4 lightSpacePos = mul(worldPos, VisibleLights[0].ShadowLightViewProj);
    lightSpacePos.xyz /= lightSpacePos.w;

    float2 uv = float2(lightSpacePos.x * 0.5f + 0.5f, -lightSpacePos.y * 0.5f + 0.5f);
    float currentDepth = lightSpacePos.z - 0.001f; // Bias 적용

    uint width, height;
    ShadowMap.GetDimensions(width, height);
    float2 texelSize = 1.0f / float2(width, height); // 섀도우 맵의 해상도 역수
    
    float shadow = 0.0f;

    // 3x3 주변 텍셀을 샘플링하여 평균 계산
    for (int x = -1; x <= 1; ++x)
    {
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize;
            shadow += ShadowMap.SampleCmpLevelZero(ShadowSampler, uv + offset, currentDepth);
        }
    }

    return shadow / 9.0f;
}

float SampleShadowPoissonDisk(float4 worldPos)
{
    float4 lightSpacePos = mul(worldPos, VisibleLights[0].ShadowLightViewProj);
    lightSpacePos.xyz /= lightSpacePos.w;

    float2 uv = float2(lightSpacePos.x * 0.5f + 0.5f, -lightSpacePos.y * 0.5f + 0.5f);
    float currentDepth = lightSpacePos.z - 0.001f; // Bias 적용

    uint width, height;
    ShadowMap.GetDimensions(width, height);
    float2 texelSize = 1.0f / float2(width, height); // 섀도우 맵의 해상도 역수
    
    float shadow = 0.0f;
    float spread = 3.5f;
    
    // Poisson Disk Distribution에 의한 16개의 샘플 오프셋
    const float2 poissonDisk[16] =
    {
        float2(-0.94201624, -0.39906216),  float2(0.94558609, -0.76890725),
        float2(-0.094184101, -0.92938870), float2(0.34495938, 0.29387760),
        float2(-0.91588581, 0.45771432),   float2(-0.81544232, -0.87912464),
        float2(-0.38440307, 0.95628987),   float2(0.20334582, -0.66986957),
        float2(0.11558417, 0.82333505),    float2(0.18510671, 0.47451193),
        float2(-0.71677703, 0.13222270),   float2(-0.23241232, -0.01105474),
        float2(0.47545300, 0.79539304),    float2(0.51189273, -0.24621427),
        float2(0.67251712, 0.42839893),    float2(0.70830405, -0.82433983)
    };
    
    // Random Rotation: 각 픽셀마다 다른 패턴이 되도록 샘플링 데이터 회전
    // 화이트 노이즈 생성
    float angle = frac(sin(dot(uv, float2(12.9898, 78.233))) * 43758.5453) * 6.283185;
    float2x2 rotationMatrix = float2x2(cos(angle), -sin(angle), sin(angle), cos(angle));
    
    for (int i = 0; i < 16; ++i)
    {
        float2 offset = mul(poissonDisk[i], rotationMatrix) * texelSize * spread;
        shadow += ShadowMap.SampleCmpLevelZero(ShadowSampler, uv + offset, currentDepth);
    }

    return shadow / 16.0f;
}

float ComputeDistanceAttenuation(float Distance, float Radius, float FalloffExponent)
{
    if (Radius <= 0.0f)
    {
        return 0.0f;
    }

    const float T = saturate(1.0f - (Distance / max(Radius, 1.0e-4f)));
    return pow(T, max(FalloffExponent, 1.0e-4f));
}

void AccumulateDirectLight(float3 WorldPos, float3 N, float3 V, float3 L, float3 LightContribution, inout FLightingResult Result)
{
#if defined(LIGHTING_MODEL_TOON)
    // --- Diffuse: 3-band 계단 처리 ---
    const float HalfLambert = dot(N, L) * 0.5f + 0.5f;    

    float ToonDiffuse;
    if (HalfLambert > 0.75f)
        ToonDiffuse = 1.0f;       // 밝은 영역
    else if (HalfLambert > 0.4f)
        ToonDiffuse = 0.6f;       // 중간 영역
    else
        ToonDiffuse = 0.15f;      // 그림자 영역

    Result.Diffuse += LightContribution * ToonDiffuse;

    /*
    const float3 H = normalize(L + V);
    const float NdotH = saturate(dot(N, H));
    const float ToonSpec = step(0.97f, pow(NdotH, max(Shininess, 1.0e-4f)));
    Result.Specular += SpecularColor * LightContribution * ToonSpec;
    
    const float RimDot = 1.0f - saturate(dot(N, V));
    // step으로 끊어서 툰 느낌 유지
    const float RimIntensity = step(0.6f, RimDot * saturate(dot(L, V) + 0.5f));
    Result.Diffuse += LightContribution * RimIntensity * 0.4f;
    */    
    
#else
    const float NdotL = saturate(dot(N, L) * 0.5f + 0.5f);
    Result.Diffuse += LightContribution * NdotL;

#if defined(LIGHTING_MODEL_GOURAUD) || defined(LIGHTING_MODEL_PHONG)
    const float3 H = normalize(L + V);
    const float SpecularPower = pow(saturate(dot(N, H)), max(Shininess, 1.0e-4f));
    Result.Specular += SpecularColor * LightContribution * SpecularPower;
#endif
#endif
}

void AccumulateVisiblePointLights(float3 WorldPos, float3 N, float3 V, float2 ScreenPos, inout FLightingResult Result)
{
    if (VisibleLightCount == 0u || TileCountX == 0u || TileCountY == 0u || TileSize == 0u || MaxLightsPerTile == 0u)
    {
        return;
    }

    const uint TileX = min((uint)ScreenPos.x / TileSize, TileCountX - 1u);
    const uint TileY = min((uint)ScreenPos.y / TileSize, TileCountY - 1u);
    const uint TileIndex = TileY * TileCountX + TileX;

    const uint LocalCount = min(TileVisibleLightCount[TileIndex], MaxLightsPerTile);
    const uint TileOffset = TileIndex * MaxLightsPerTile;

    [loop]
    for (uint VisIdx = 0u; VisIdx < LocalCount; ++VisIdx)
    {
        const uint LightIndex = TileVisibleLightIndices[TileOffset + VisIdx];
        if (LightIndex >= VisibleLightCount)
        {
            continue;
        }

        const FVisibleLightData Light = VisibleLights[LightIndex];
        const float3 ToLight = Light.WorldPos - WorldPos;
        const float Distance = length(ToLight);
        if (Distance <= 1.0e-4f || Distance >= Light.Radius)
        {
            continue;
        }

        const float3 L = ToLight / Distance;
        float Att = ComputeDistanceAttenuation(Distance, Light.Radius, Light.RadiusFalloff);
        if (Att <= 0.0f)
        {
            continue;
        }

        if (Light.Type == LIGHT_TYPE_SPOT)
        {
            const float3 SpotDir = normalize(Light.Direction);
            const float CosAngle = dot(SpotDir, -L);
            const float ConeRange = max(Light.SpotInnerCos - Light.SpotOuterCos, 1.0e-4f);
            Att *= saturate((CosAngle - Light.SpotOuterCos) / ConeRange);
            if (Att <= 0.0f)
            {
                continue;
            }
        }

        AccumulateDirectLight(WorldPos, N, V, L, Light.Color * Light.Intensity * Att, Result);
    }
}

FLightingResult EvaluateLightingFromWorld(float3 WorldPos, float3 WorldNormal, float2 ScreenPos)
{
    FLightingResult Result;
    Result.Diffuse = 0.0f.xxx;
    Result.Specular = 0.0f.xxx;

    const float3 N = normalize(WorldNormal);
    const float3 V = normalize(CameraPosition - WorldPos);

    float3 AmbientContribution = DEFAULT_AMBIENT_COLOR;
    uint bHasAmbientLight = 0u;

    [loop]
    for (uint LightIndex = 0u; LightIndex < SceneGlobalLightCount; ++LightIndex)
    {
        const FGPULight Light = GlobalLights[LightIndex];
        const float3 LightColor = Light.Color * Light.Intensity;

        if (Light.Type == LIGHT_TYPE_AMBIENT)
        {
            if (bHasAmbientLight == 0u)
            {
                AmbientContribution = 0.0f.xxx;
                bHasAmbientLight = 1u;
            }

            AmbientContribution += LightColor;
            continue;
        }

        if (Light.Type == LIGHT_TYPE_DIRECTIONAL)
        {
            //float ShadowFactor = SampleShadow(float4(WorldPos, 1.0f), LightViewProj);

            AccumulateDirectLight(WorldPos, N, V, normalize(Light.Direction), LightColor, Result);
        }
    }

    Result.Diffuse += AmbientContribution;
    AccumulateVisiblePointLights(WorldPos, N, V, ScreenPos, Result);

    return Result;
}
FLightingResult EvaluateLightingFromWorldVertex(float3 WorldPos, float3 WorldNormal)
{
    FLightingResult Result;
    Result.Diffuse = DEFAULT_AMBIENT_COLOR;
    Result.Specular = 0.0f.xxx;

    const float3 N = normalize(WorldNormal);
    const float3 V = normalize(CameraPosition - WorldPos);

    float3 AmbientAccum = 0.0f.xxx;
    uint HasAmbient = 0u;

    // =========================
    // 1. Scene Global Lights (Directional + Ambient)
    // =========================
    [loop]
    for (uint i = 0u; i < SceneGlobalLightCount; ++i)
    {
        const FGPULight Light = GlobalLights[i];
        const float3 LightColor = Light.Color * Light.Intensity;

        if (Light.Type == LIGHT_TYPE_AMBIENT)
        {
            if (HasAmbient == 0u)
            {
                AmbientAccum = 0.0f.xxx;
                HasAmbient = 1u;
            }

            AmbientAccum += LightColor;
            continue;
        }

        if (Light.Type == LIGHT_TYPE_DIRECTIONAL)
        {
            const float3 L = normalize(Light.Direction);
            AccumulateDirectLight(WorldPos, N, V, L, LightColor, Result);
        }
    }

    Result.Diffuse += AmbientAccum;

    // =========================
    // 2. Local Lights (Point / Spot) - brute force
    // 해당 함수는 구로 셰이딩 모드에서만 들어오고, 픽셀 단위 컬링을 쓰지 않기 때문에 전체 순회
    // =========================
    [loop]
    for (uint j = 0u; j < VisibleLightCount; ++j)
    {
        const FVisibleLightData Light = VisibleLights[j];

        const float3 ToLight = Light.WorldPos - WorldPos;
        const float Dist = length(ToLight);

        if (Dist <= 1.0e-4f || Dist >= Light.Radius)
            continue;

        const float3 L = ToLight / Dist;

        float Att = ComputeDistanceAttenuation(Dist, Light.Radius, Light.RadiusFalloff);
        
        if (Att <= 0.0f)
            continue;

        if (Light.Type == LIGHT_TYPE_SPOT)
        {
            const float3 SpotDir = normalize(Light.Direction);
            const float CosAngle = dot(SpotDir, -L);
            const float ConeRange = max(Light.SpotInnerCos - Light.SpotOuterCos, 1.0e-4f);

            Att *= saturate((CosAngle - Light.SpotOuterCos) / ConeRange);
            if (Att <= 0.0f)
                continue;
        }

        AccumulateDirectLight(WorldPos, N, V, L, Light.Color * Light.Intensity * Att, Result);
    }

    return Result;
}

float3 ApplyLighting(FUberSurfaceData Surface, FLightingResult Lighting)
{
    return Surface.Albedo * Lighting.Diffuse + Lighting.Specular;
}

#if defined(MATERIAL_DOMAIN_DECAL)

cbuffer UberDecal : register(b5)
{
    row_major float4x4 InvDecalWorld;
}

struct FUberDecalPSOutput
{
    float4 Color : SV_TARGET0;
    float4 Normal : SV_TARGET1;
};

FUberSurfaceData EvaluateProjectedDecal(FUberPSInput Input)
{
    FUberSurfaceData Surface;
    Surface.WorldPos = Input.WorldPos;
    Surface.WorldNormal = normalize(Input.WorldNormal);
    Surface.bIsEmissive = any(EmissiveColor > 0.0f) ? 1u : 0u;

    const float4 LocalPos = mul(float4(Input.WorldPos, 1.0f), InvDecalWorld);
    clip(0.5f - abs(LocalPos.xyz));

    Surface.UV = float2(LocalPos.y + 0.5f, 1.0f - (LocalPos.z + 0.5f));
    Surface.DiffuseSample = DiffuseMap.Sample(SampleState, Surface.UV);
    Surface.WorldNormal = ResolveSurfaceWorldNormal(Input, Surface.UV, Surface.WorldNormal);
    Surface.Albedo = BaseColor * Surface.DiffuseSample.rgb;

    return Surface;
}

FUberPSInput mainVS(FUberVSInput Input)
{
    FUberPSInput Output = BuildSurfaceVertex(Input);
    Output.ClipPos.z -= 0.0001f;
    return Output;
}

FUberDecalPSOutput mainPS(FUberPSInput Input)
{
    FUberSurfaceData Surface = EvaluateProjectedDecal(Input);
    Surface.Albedo *= PrimitiveColor.rgb;

    const float Alpha = saturate(Opacity * PrimitiveColor.a * Surface.DiffuseSample.a);
    clip(Alpha - 0.001f);

    float3 FinalColor = Surface.Albedo;
    if (bLightingEnabled > 0.5f)
    {
        const FLightingResult Lighting = EvaluateLightingFromWorld(Surface.WorldPos, Surface.WorldNormal, Input.ClipPos.xy);
        FinalColor = ApplyLighting(Surface, Lighting);
    }

    if (Surface.bIsEmissive != 0u)
    {
        FinalColor += EmissiveColor * Surface.DiffuseSample.rgb * PrimitiveColor.rgb;
    }

    if (bIsWireframe > 0.5f)
    {
        FinalColor = WireframeRGB;
    }

    FUberDecalPSOutput Output;
    Output.Color = float4(FinalColor, Alpha);
    Output.Normal = float4(Surface.WorldNormal * 0.5f + 0.5f, Alpha);
    return Output;
}

#else

FUberPSInput mainVS(FUberVSInput Input)
{
    FUberPSInput Output = BuildSurfaceVertex(Input);

#if defined(LIGHTING_MODEL_GOURAUD)
    const FLightingResult Lighting = EvaluateLightingFromWorldVertex(Output.WorldPos, Output.WorldNormal);
    Output.VertexDiffuseLighting = Lighting.Diffuse;
    Output.VertexSpecularLighting = Lighting.Specular;
#endif

    return Output;
}

FUberPSOutput mainPS(FUberPSInput Input)
{
    const FUberSurfaceData Surface = EvaluateSurface(Input);

    FLightingResult Lighting;
    
    float ShadowFactor;
    float ShadowFactorPCF = SampleShadow(float4(Surface.WorldPos, 1.0f));
    float ShadowFactorPoisson = SampleShadowPoissonDisk(float4(Surface.WorldPos, 1.0f));
    
    ShadowFactor = ShadowFactorPoisson;
    //return ComposeOutput(Surface, float3(ShadowFactor, ShadowFactor, ShadowFactor));

#if defined(LIGHTING_MODEL_GOURAUD)
    Lighting.Diffuse = Input.VertexDiffuseLighting;
    Lighting.Specular = Input.VertexSpecularLighting;
    
#elif defined(LIGHTING_MODEL_LAMBERT)
    Lighting = EvaluateLightingFromWorld(Surface.WorldPos, Surface.WorldNormal, Input.ClipPos.xy);
    Lighting.Specular = 0.0f.xxx;
#else
    // TOON | PHONG (함수 내에서 내부적으로 계산 흐름 분리)
    Lighting = EvaluateLightingFromWorld(Surface.WorldPos, Surface.WorldNormal, Input.ClipPos.xy);

#endif
    Lighting.Diffuse *= ShadowFactor;
    Lighting.Specular *= ShadowFactor;
    
    return ComposeOutput(Surface, ApplyLighting(Surface, Lighting));
}

#endif
