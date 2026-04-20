#define LIGHT_CULLING_TILE_SIZE 16
#define LIGHT_CULLING_MAX_LIGHTS_PER_TILE 64

struct FLightDataCS
{
    float3 WorldPos;
    float Radius;
    float3 Color;
    float Intensity;
    float RadiusFalloff;
    float3 Padding;
};

cbuffer LightCullingConstants : register(b0)
{
    row_major float4x4 View;
    row_major float4x4 Projection;
    uint LightCount;
    uint TileCountX;
    uint TileCountY;
    uint TileSize;
    float ViewportWidth;
    float ViewportHeight;
    float2 Padding;
};

StructuredBuffer<FLightDataCS> SceneLights : register(t0);
RWStructuredBuffer<uint> TileVisibleLightCount : register(u0);
RWStructuredBuffer<uint> TileVisibleLightIndices : register(u1);

[numthreads(8, 8, 1)]
void mainCS(uint3 DispatchThreadID : SV_DispatchThreadID)
{
    if (DispatchThreadID.x >= TileCountX || DispatchThreadID.y >= TileCountY)
    {
        return;
    }

    const uint TileIndex = DispatchThreadID.y * TileCountX + DispatchThreadID.x;
    const uint TileStartOffset = TileIndex * LIGHT_CULLING_MAX_LIGHTS_PER_TILE;

    const float2 TileMin = float2(DispatchThreadID.xy) * float(TileSize);
    const float2 TileMax = TileMin + float2(TileSize, TileSize);

    uint VisibleCount = 0;
    for (uint LightIndex = 0; LightIndex < LightCount; ++LightIndex)
    {
        const FLightDataCS Light = SceneLights[LightIndex];
        const float4 ViewPosition = mul(float4(Light.WorldPos, 1.0f), View);
        const float EyeDepth = ViewPosition.x; // 전방 = View X축

        if (EyeDepth + Light.Radius <= 1e-4f)
        {
            continue;
        }

        if (EyeDepth < Light.Radius)
        {
            if (VisibleCount < LIGHT_CULLING_MAX_LIGHTS_PER_TILE)
            {
                TileVisibleLightIndices[TileStartOffset + VisibleCount] = LightIndex;
                VisibleCount++;
            }
            continue;
        }

        const float4 ClipPosition = mul(ViewPosition, Projection);
        if (ClipPosition.w <= 0.0f)  // ClipPos.w = EyeDepth이므로 사실상 중복이지만 안전장치
        {
            continue;
        }

        const float2 Ndc = ClipPosition.xy / ClipPosition.w;
        const float2 ScreenPosition = float2(
                                        (Ndc.x * 0.5f + 0.5f) * ViewportWidth,
                                        (-Ndc.y * 0.5f + 0.5f) * ViewportHeight);

        // Projection[1][0] = XScale (Row1, Col0) → Y뷰→X클립
        // Projection[2][1] = YScale (Row2, Col1) → Z뷰→Y클립
        // ProjectedRadius는 YScale 기준 (수직 FOV 기준)
        const float YScale = Projection[2][1]; // ViewPos.z * YScale → ClipPos.y
        const float ProjectedRadius = (Light.Radius / EyeDepth) * YScale * (ViewportHeight * 0.5f);
        if (ProjectedRadius <= 0.0f)
        {
            continue;
        }

        const float2 ClosestPoint = clamp(ScreenPosition, TileMin, TileMax);
        const float2 Delta = ScreenPosition - ClosestPoint;
        const bool bIntersectsTile = dot(Delta, Delta) <= (ProjectedRadius * ProjectedRadius);
        if (!bIntersectsTile)
        {
            continue;
        }

        if (VisibleCount < LIGHT_CULLING_MAX_LIGHTS_PER_TILE)
        {
            TileVisibleLightIndices[TileStartOffset + VisibleCount] = LightIndex;
            VisibleCount++;
        }
    }

    TileVisibleLightCount[TileIndex] = VisibleCount;
}
