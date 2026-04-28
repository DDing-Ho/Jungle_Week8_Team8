#include "UberSurface.hlsli"
#include "ShadowSample.hlsli"

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
    uint bCastShadows;

    int ShadowMapIndex;
    float ShadowBias;  
    float Padding0;
    float Padding1;
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
    float3 WorldPos;
    float Radius;
    
    float3 Color;
    float Intensity;
    
    float RadiusFalloff;
    uint Type;
    float SpotInnerCos;
    float SpotOuterCos;

    float3 Direction;
    uint bCastShadows;

    int ShadowMapIndex;
    float ShadowBias;
    float Padding0;
    float Padding1;
};

StructuredBuffer<FVisibleLightData> VisibleLights : register(t8);
StructuredBuffer<uint> TileVisibleLightCount : register(t9);
StructuredBuffer<uint> TileVisibleLightIndices : register(t10);

// ─────────────────── Shadows ───────────────────

struct FSpotShadowConstants
{
    row_major float4x4 LightViewProj;
    float4 AtlasRect; // xy = offset, zw = scale
    float ShadowResolution;
    float ShadowBias;
    float SpotShadowSharpen;
    float Padding;
};

cbuffer SpotShadowInfo : register(b6)
{
    uint SpotShadowCount;
    uint SpotShadowFilterType;
    float2 _SpotShadowInfoPad0;
}

#define MAX_CASCADE_COUNT 4
static const uint SHADOW_MODE_CSM = 0u;
static const uint SHADOW_MODE_PSM = 1u;
static const float PSM_SHADOW_BIAS_SCALE = 500.0f;

cbuffer DirectionalShadowInfo : register(b7)
{
    row_major float4x4 LightViewProj[MAX_CASCADE_COUNT];
    float4 SplitDistances;
    float4 CascadeRadius;
    
    float ShadowBias;
    float ShadowSlopeBias;
    float ShadowSharpen;
    uint bCascadeDebug;
    
    uint bHasShadowMap;
    uint ShadowFilterType;
    uint ShadowMode;
    float Padding0;
}

StructuredBuffer<FSpotShadowConstants> SpotShadowData : register(t11);
Texture2D<float> SpotShadowMap : register(t12);
Texture2D<float> DirectionalShadowMap : register(t13);
// TODO: Point(t14)

Texture2D<float2> SpotShadowVSMMap : register(t15);
Texture2D<float2> DirectionalShadowVSMMap : register(t16);
//Texture2D<float2> PointShadowVSMMap : register(t17);

static const int kCascadeShadowResoultion = 2048; // ShadowPass::CascadeShadowResolution과 일치
static const int kDirectionalAtlasResolution = 4096;

// ─────────────────── Lights ───────────────────

static const uint LIGHT_TYPE_DIRECTIONAL = 0u;
static const uint LIGHT_TYPE_POINT = 1u;
static const uint LIGHT_TYPE_SPOT = 2u;
static const uint LIGHT_TYPE_AMBIENT = 3u;
static const float3 DEFAULT_AMBIENT_COLOR = float3(0.02f, 0.02f, 0.02f);

static const uint SHADOW_FILTER_TYPE_PCF = 0u;
static const uint SHADOW_FILTER_TYPE_VSM = 1u;

float4 GetDirectionalCascadeAtlasRect(int CascadeIndex)
{
    if (CascadeIndex == 0) return float4(0.0f, 0.0f, 0.5f, 0.5f);
    if (CascadeIndex == 1) return float4(0.5f, 0.0f, 0.5f, 0.5f);
    if (CascadeIndex == 2) return float4(0.0f, 0.5f, 0.5f, 0.5f);
    return float4(0.5f, 0.5f, 0.5f, 0.5f);
}

int GetCascadeIndex(float3 WorldPos)
{
    float ViewDepth = mul(float4(WorldPos, 1.0f), View).x;

    int CascadeIndex = (int)(step(SplitDistances.x, ViewDepth));
    CascadeIndex += step(SplitDistances.y, ViewDepth);
    CascadeIndex += step(SplitDistances.z, ViewDepth);
    return min(CascadeIndex, MAX_CASCADE_COUNT - 1);
}

// 뷰 공간 깊이로 Cascade Index를 결정한다.
float SampleDirectionalShadowAtIndex(float3 WorldPos, float3 N, float3 L, int ShadowIndex)
{
    const float BiasScale = (ShadowMode == SHADOW_MODE_PSM) ? PSM_SHADOW_BIAS_SCALE : max(SplitDistances.w, 1.0e-4f);
    const float NormalizedBias = ShadowBias / BiasScale;
    const float NormalizedSlopeBias = ShadowSlopeBias / BiasScale;
    
    float CosTheta = saturate(dot(N, L));
    CosTheta = max(CosTheta, 1e-4f);
    float TanTheta = sqrt(1.0 - CosTheta * CosTheta) / CosTheta;
    TanTheta = min(TanTheta, 2.0f);
    
    const float Bias = NormalizedBias + (NormalizedSlopeBias * TanTheta);
    const float NormalOffsetScale = 0.3f; // Noraml Offset Bias
    const float3 OffsetWorldPos = WorldPos + N * NormalOffsetScale * (1.0f - CosTheta);
    const float4 ShadowClip = mul(float4(OffsetWorldPos, 1.0f), LightViewProj[ShadowIndex]);
    
    const float W = (ShadowClip.w <= 1.0e-5f) ? 1.0f : ShadowClip.w;
    const float3 ShadowNDC = ShadowClip.xyz / W;
    const float InBounds =
        step(abs(ShadowNDC.x), 1.0f) *
        step(abs(ShadowNDC.y), 1.0f) *
        step(0.0f, ShadowNDC.z) *
        step(ShadowNDC.z, 1.0f) *
        step(1.0e-5f, ShadowClip.w);
    if (InBounds <= 0.0f)
    {
        return 1.0f;
    }
        
    float2 LocalUV = float2(ShadowNDC.x * 0.5f + 0.5f, ShadowNDC.y * -0.5f + 0.5f);
    float4 AtlasRect = GetDirectionalCascadeAtlasRect(ShadowIndex);
    float2 AtlasUV = AtlasRect.xy + LocalUV * AtlasRect.zw;
    
    int2 AtlasSize = int2(kDirectionalAtlasResolution, kDirectionalAtlasResolution);
    
    if (ShadowFilterType == SHADOW_FILTER_TYPE_PCF)
        return SampleShadowPoissonDisk(AtlasUV, ShadowNDC.z - Bias, DirectionalShadowMap, AtlasSize, ShadowSharpen);
    else
        return SampleShadowVSM(AtlasUV, ShadowNDC.z - Bias, DirectionalShadowVSMMap, AtlasSize);
}

float ComputeDirectionalShadowFactor(float3 WorldPos, float3 N, float3 L)
{
    if (bHasShadowMap == 0u)
    {
        return 1.0f;
    }

    const int ShadowIndex = (ShadowMode == SHADOW_MODE_PSM) ? 0 : GetCascadeIndex(WorldPos);
    return SampleDirectionalShadowAtIndex(WorldPos, N, L, ShadowIndex);
}

struct FLightingResult
{
    float3 Diffuse;
    float3 Specular;
};

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

float ComputeSpotShadowFactor(float3 WorldPos, uint bCastShadows, int ShadowMapIndex, float LightShadowBias)
{
    if (bCastShadows == 0u || ShadowMapIndex < 0)
    {
        return 1.0f;
    }

    const uint ShadowSlice = (uint)ShadowMapIndex;
    if (ShadowSlice >= SpotShadowCount)
    {
        return 1.0f;
    }

    const FSpotShadowConstants Shadow = SpotShadowData[ShadowSlice];

    const float4 ShadowClip = mul(float4(WorldPos, 1.0f), Shadow.LightViewProj);
    if (ShadowClip.w <= 1.0e-5f)
    {
        return 1.0f;
    }

    const float3 ShadowNDC = ShadowClip.xyz / ShadowClip.w;
    if (ShadowNDC.x < -1.0f || ShadowNDC.x > 1.0f ||
        ShadowNDC.y < -1.0f || ShadowNDC.y > 1.0f ||
        ShadowNDC.z < 0.0f || ShadowNDC.z > 1.0f)
    {
        return 1.0f;
    }

    const float2 LocalUV = float2(
        ShadowNDC.x * 0.5f + 0.5f,
        0.5f - ShadowNDC.y * 0.5f);

    const float2 AtlasUV = Shadow.AtlasRect.xy + LocalUV * Shadow.AtlasRect.zw;
    const int2 AtlasSize = int2(4096, 4096);
    
    const float CurrentDepth = ShadowNDC.z;
    const float Bias = max(LightShadowBias, Shadow.ShadowBias);
    
    if (SpotShadowFilterType == SHADOW_FILTER_TYPE_PCF)
        return SampleShadowPoissonDisk(AtlasUV, CurrentDepth - Bias, SpotShadowMap, AtlasSize, Shadow.SpotShadowSharpen);
    else
        return SampleShadowVSM(AtlasUV, CurrentDepth - Bias, SpotShadowVSMMap, AtlasSize);
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

            Att *= ComputeSpotShadowFactor(WorldPos, Light.bCastShadows, Light.ShadowMapIndex, Light.ShadowBias);
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
            float ShadowFactor = 1.0f;
            if (Light.bCastShadows != 0u)
            {
                ShadowFactor = ComputeDirectionalShadowFactor(WorldPos, N, normalize(Light.Direction));
            }

            AccumulateDirectLight(WorldPos, N, V, normalize(Light.Direction), LightColor * ShadowFactor, Result);

            if (bCascadeDebug != 0u && ShadowMode == SHADOW_MODE_CSM)
            {
                int CascadeIndex = GetCascadeIndex(WorldPos);
                float3 DebugColors[4] = {
                    float3(1.0, 0.2, 0.2), // Reddish
                    float3(0.2, 1.0, 0.2), // Greenish
                    float3(0.2, 0.2, 1.0), // Blueish
                    float3(1.0, 1.0, 0.2)  // Yellowish
                };
                Result.Diffuse = lerp(Result.Diffuse, DebugColors[CascadeIndex], 0.5f);
            }
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
            
            float ShadowFactor = 1.0f;
            if (Light.bCastShadows != 0u)
            {
                ShadowFactor = ComputeDirectionalShadowFactor(WorldPos, N, normalize(Light.Direction));
            }
            AccumulateDirectLight(WorldPos, N, V, L, LightColor * ShadowFactor, Result);

            if (bCascadeDebug != 0u && ShadowMode == SHADOW_MODE_CSM)
            {
                int CascadeIndex = GetCascadeIndex(WorldPos);
                float3 DebugColors[4] = {
                    float3(1.0, 0.2, 0.2), // Reddish
                    float3(0.2, 1.0, 0.2), // Greenish
                    float3(0.2, 0.2, 1.0), // Blueish
                    float3(1.0, 1.0, 0.2)  // Yellowish
                };
                Result.Diffuse = lerp(Result.Diffuse, DebugColors[CascadeIndex], 0.5f);
            }
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
    return ComposeOutput(Surface, ApplyLighting(Surface, Lighting));
}

#endif
