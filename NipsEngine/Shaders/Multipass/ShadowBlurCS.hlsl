cbuffer FBlurConstants : register(b10)
{
    uint BlurDirection; // 0 = Horizontal, 1 = Vertical
    uint SliceCount;
    uint Pad0;
    uint Pad1;
};

Texture2DArray<float2> InputMap : register(t14);
RWTexture2DArray<float2> OutputMap : register(u0);

static const int KERNEL_RADIUS = 5;
static const float GaussWeights[11] =
{
    0.0093f, 0.0280f, 0.0654f, 0.1210f, 0.1762f,
    0.1994f,
    0.1762f, 0.1210f, 0.0654f, 0.0280f, 0.0093f
};

#define TILE_SIZE   8
#define CACHE_SIZE  (TILE_SIZE + KERNEL_RADIUS * 2) // 18

groupshared float2 Cache[TILE_SIZE][CACHE_SIZE];

[numthreads(TILE_SIZE, TILE_SIZE, 1)]
void mainCS(
    uint3 GroupID : SV_GroupID,
    uint3 GroupThreadID : SV_GroupThreadID,
    uint3 DispatchID : SV_DispatchThreadID)
{
    uint SliceIndex = GroupID.z;
    if (SliceIndex >= SliceCount)
        return;

    int Width, Height, Elements;
    InputMap.GetDimensions(Width, Height, Elements);

    int2 TexelBase = int2(GroupID.xy) * TILE_SIZE;

    if (BlurDirection == 0)
    {
        // Horizontal
        int CacheColsPerThread = (CACHE_SIZE + TILE_SIZE - 1) / TILE_SIZE; // 3

        for (int c = 0; c < CacheColsPerThread; ++c)
        {
            int CacheCol = (int) GroupThreadID.x * CacheColsPerThread + c;
            if (CacheCol >= CACHE_SIZE)
                break;

            int TexCol = clamp(TexelBase.x + CacheCol - KERNEL_RADIUS, 0, Width - 1);
            int TexRow = clamp(TexelBase.y + (int) GroupThreadID.y, 0, Height - 1);

            Cache[GroupThreadID.y][CacheCol] = InputMap.Load(int4(TexCol, TexRow, SliceIndex, 0));
        }
    }
    else
    {
        // Vertical
        int CacheRowsPerThread = (CACHE_SIZE + TILE_SIZE - 1) / TILE_SIZE; // 3

        for (int r = 0; r < CacheRowsPerThread; ++r)
        {
            int CacheRow = (int) GroupThreadID.y * CacheRowsPerThread + r;
            if (CacheRow >= CACHE_SIZE)
                break;

            int TexRow = clamp(TexelBase.y + CacheRow - KERNEL_RADIUS, 0, Height - 1);
            int TexCol = clamp(TexelBase.x + (int) GroupThreadID.x, 0, Width - 1);

            Cache[GroupThreadID.x][CacheRow] = InputMap.Load(int4(TexCol, TexRow, SliceIndex, 0));
        }
    }

    GroupMemoryBarrierWithGroupSync();

    if ((int) DispatchID.x >= Width || (int) DispatchID.y >= Height)
        return;

    // 가우시안 합산
    float2 BlurResult = float2(0.0f, 0.0f);

    if (BlurDirection == 0)
    {
        int CacheCenter = (int) GroupThreadID.x + KERNEL_RADIUS;
        for (int k = -KERNEL_RADIUS; k <= KERNEL_RADIUS; ++k)
        {
            BlurResult += Cache[GroupThreadID.y][CacheCenter + k] * GaussWeights[k + KERNEL_RADIUS];
        }
    }
    else
    {
        int CacheCenter = (int) GroupThreadID.y + KERNEL_RADIUS;
        for (int k = -KERNEL_RADIUS; k <= KERNEL_RADIUS; ++k)
        {
            BlurResult += Cache[GroupThreadID.x][CacheCenter + k] * GaussWeights[k + KERNEL_RADIUS];
        }
    }

    OutputMap[uint3(DispatchID.xy, SliceIndex)] = BlurResult;
}