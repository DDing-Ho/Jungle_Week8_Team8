#include "ShadowAtlasManager.h"

#include <algorithm>
#include <cmath>

TArray<uint8> FShadowAtlasManager::SpotCellOccupancy;
TArray<FSpotAtlasSlotDesc> FShadowAtlasManager::ActiveSpotSlots;
TArray<FDirectionalAtlasSlotDesc> FShadowAtlasManager::DirectionalCascadeSlots;

bool FShadowAtlasManager::Initialize(ID3D11Device* Device)
{
    if (Device == nullptr)
    {
        return false;
    }
    
    // 이미 만들어진 atlas가 있으면 프레임 간 재사용합니다.
    if (SpotAtlasTexture && SpotAtlasDSV && SpotAtlasSRV)
    {
        return true;
    }

    // Depth를 DSV/SRV 둘 다에서 쓰기 위해 typeless texture를 만듭니다.
    D3D11_TEXTURE2D_DESC TextureDesc = {};
    TextureDesc.Width = SpotAtlasResolution;
    TextureDesc.Height = SpotAtlasResolution;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = 1;
    TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    
    if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, SpotAtlasTexture.GetAddressOf())))
    {
        return false;
    }
    
    D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
    DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
    DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    DSVDesc.Flags = 0;
    DSVDesc.Texture2D.MipSlice = 0;
    if (FAILED(Device->CreateDepthStencilView(SpotAtlasTexture.Get(), &DSVDesc, SpotAtlasDSV.GetAddressOf())))
    {
        Release();        
        return false;
    }
    
    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SRVDesc.Texture2D.MostDetailedMip = 0;
    SRVDesc.Texture2D.MipLevels = 1;
    if (FAILED(Device->CreateShaderResourceView(SpotAtlasTexture.Get(), &SRVDesc, SpotAtlasSRV.GetAddressOf())))
    {
        Release();        
        return false;
    }
    
    return true;
}

bool FShadowAtlasManager::InitializeDirectionalAtlas(ID3D11Device* Device)
{
    if (Device == nullptr)
    {
        return false;
    }
    
    if (DirectionalAtlasTexture && DirectionalAtlasDSV && DirectionalAtlasSRV)
    {
        return true;
    }
    
    D3D11_TEXTURE2D_DESC TextureDesc = {};
    TextureDesc.Width = DirectionalAtlasResolution;
    TextureDesc.Height = DirectionalAtlasResolution;
    TextureDesc.MipLevels = 1;
    TextureDesc.ArraySize = 1;
    TextureDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    TextureDesc.SampleDesc.Count = 1;
    TextureDesc.SampleDesc.Quality = 0;
    TextureDesc.Usage = D3D11_USAGE_DEFAULT;
    TextureDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    
    if (FAILED(Device->CreateTexture2D(&TextureDesc, nullptr, DirectionalAtlasTexture.GetAddressOf())))
    {
        return false;
    }
    
    D3D11_DEPTH_STENCIL_VIEW_DESC DSVDesc = {};
    DSVDesc.Format = DXGI_FORMAT_D32_FLOAT;
    DSVDesc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    DSVDesc.Flags = 0;
    DSVDesc.Texture2D.MipSlice = 0;
    if (FAILED(Device->CreateDepthStencilView(DirectionalAtlasTexture.Get(), &DSVDesc, DirectionalAtlasDSV.GetAddressOf())))
    {
        DirectionalAtlasTexture.Reset();
        return false;
    }
    
    D3D11_SHADER_RESOURCE_VIEW_DESC SRVDesc = {};
    SRVDesc.Format = DXGI_FORMAT_R32_FLOAT;
    SRVDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    SRVDesc.Texture2D.MostDetailedMip = 0;
    SRVDesc.Texture2D.MipLevels = 1;
    if (FAILED(Device->CreateShaderResourceView(DirectionalAtlasTexture.Get(), &SRVDesc, DirectionalAtlasSRV.GetAddressOf())))
    {
        DirectionalAtlasDSV.Reset();
        DirectionalAtlasTexture.Reset();      
        return false;
    }
    
    return true;
}

void FShadowAtlasManager::Release()
{
    SpotAtlasSRV.Reset();
    SpotAtlasDSV.Reset();
    SpotAtlasTexture.Reset();

    DirectionalAtlasTexture.Reset();
    DirectionalAtlasDSV.Reset();
    DirectionalAtlasSRV.Reset();
}

void FShadowAtlasManager::BeginSpotFrame()
{
    const uint32 CellCount = SpotAtlasCellsPerRow * SpotAtlasCellsPerRow;
    
    if (SpotCellOccupancy.size() != CellCount)
    {
        SpotCellOccupancy.resize(CellCount, 0u);
    }
    
    std::fill(SpotCellOccupancy.begin(), SpotCellOccupancy.end(), 0u);
    ActiveSpotSlots.clear();
}

uint32 FShadowAtlasManager::SnapSpotTileSize(float RequestedResolution)
{
    float Clamped = std::clamp(
        RequestedResolution,
        static_cast<float>(MinSpotTileResolution),
        static_cast<float>(MaxSpotTileResolution));
    
    uint32 Lower = MinSpotTileResolution;
    while ((Lower << 1u) <= MaxSpotTileResolution && static_cast<float>(Lower << 1u) <= Clamped)
    {
        Lower <<= 1u;
    }
    
    uint32 Upper = Lower;
    if (Upper < MaxSpotTileResolution)
    {
        Upper <<= 1u;
    }

    const float LowerDelta = std::fabs(Clamped - static_cast<float>(Lower));
    const float UpperDelta = std::fabs(static_cast<float>(Upper) - Clamped);

    // tie면 품질을 위해 큰 쪽을 선택합니다.
    return (UpperDelta <= LowerDelta) ? Upper : Lower;
}

bool FShadowAtlasManager::RequestSpotSlot(uint32 DesiredResolution, FSpotAtlasSlotDesc& OutSlot)
{
    const uint32 TileResolution = SanitizeSpotTileSize(DesiredResolution);
    return TryAllocateSpotSlot(TileResolution, OutSlot);
}

const TArray<FSpotAtlasSlotDesc>& FShadowAtlasManager::GetActiveSpotSlots()
{
    return ActiveSpotSlots;
}

void FShadowAtlasManager::UpdateSpotSlotDebugLightId(uint32 TileIndex, int32 DebugLightId)
{
    if (TileIndex >= ActiveSpotSlots.size())
    {
        return;
    }

    ActiveSpotSlots[TileIndex].DebugLightId = DebugLightId;
}

uint32 FShadowAtlasManager::SanitizeSpotTileSize(uint32 DesiredResolution)
{
    if (DesiredResolution < MinSpotTileResolution || DesiredResolution > MaxSpotTileResolution)
    {
        return SnapSpotTileSize(static_cast<float>(DesiredResolution));
    }

    // 허용 크기 집합이 아닌 값이 들어오면 가장 가까운 허용 PoT로 보정
    switch (DesiredResolution)
    {
    case 256:
    case 512:
    case 1024:
    case 2048:
        return DesiredResolution;
    default:
        return SnapSpotTileSize(static_cast<float>(DesiredResolution));
    }
}

bool FShadowAtlasManager::TryAllocateSpotSlot(uint32 TileResolution, FSpotAtlasSlotDesc& OutSlot)
{
    const uint32 CellSpan = TileResolution / SpotCellResolution;
    if (CellSpan == 0 || CellSpan > SpotAtlasCellsPerRow)
    {
        return false;
    }
    
    // 단순: first-fit allocator
    for (uint32 CellY=0; CellY + CellSpan <= SpotAtlasCellsPerRow; ++CellY)
    {
        for (uint32 CellX = 0; CellX + CellSpan <= SpotAtlasCellsPerRow; ++CellX)
        {
            // 해당 영역 사용 가능한지 파악
            if (!IsSpotRegionFree(CellX, CellY, CellSpan))
            {
                continue;
            }
            // 해당 영역 표시
            MarkSpotRegion(CellX, CellY, CellSpan, true);
            
            const uint32 TileIndex = static_cast<uint32>(ActiveSpotSlots.size());
            BuildSpotSlotDesc(CellX, CellY, TileResolution, TileIndex, OutSlot);
            ActiveSpotSlots.push_back(OutSlot);
            return true;
        }
    }
    return false;
}

bool FShadowAtlasManager::IsSpotRegionFree(uint32 CellX, uint32 CellY, uint32 CellSpan)
{
    for (uint32 Y = CellY; Y < CellY + CellSpan; ++Y)
    {
        for (uint32 X = CellX; X < CellX + CellSpan; ++X)
        {
            const uint32 Index = Y * SpotAtlasCellsPerRow + X;
            if (Index >= SpotCellOccupancy.size() || SpotCellOccupancy[Index] != 0u)
            {
                return false;
            }
        }
    }

    return true;
}

void FShadowAtlasManager::MarkSpotRegion(uint32 CellX, uint32 CellY, uint32 CellSpan, bool bOccupied)
{
    const uint8 OccupiedValue = bOccupied ? 1u : 0u;

    for (uint32 Y = CellY; Y < CellY + CellSpan; ++Y)
    {
        for (uint32 X = CellX; X < CellX + CellSpan; ++X)
        {
            const uint32 Index = Y * SpotAtlasCellsPerRow + X;
            if (Index < SpotCellOccupancy.size())
            {
                SpotCellOccupancy[Index] = OccupiedValue;
            }
        }
    }
}

void FShadowAtlasManager::BuildSpotSlotDesc(uint32 CellX, uint32 CellY, uint32 TileResolution, uint32 TileIndex, FSpotAtlasSlotDesc& OutSlot)
{
    OutSlot.TileIndex = TileIndex;
    OutSlot.DebugLightId = -1;
    OutSlot.X = CellX * SpotCellResolution;
    OutSlot.Y = CellY * SpotCellResolution;
    OutSlot.Width = TileResolution;
    OutSlot.Height = TileResolution;
    OutSlot.AtlasRect = FVector4(
        static_cast<float>(OutSlot.X) / static_cast<float>(SpotAtlasResolution),
        static_cast<float>(OutSlot.Y) / static_cast<float>(SpotAtlasResolution),
        static_cast<float>(OutSlot.Width) / static_cast<float>(SpotAtlasResolution),
        static_cast<float>(OutSlot.Height) / static_cast<float>(SpotAtlasResolution));
}

const TArray<FDirectionalAtlasSlotDesc>& FShadowAtlasManager::GetDirectionalCascadeSlots()
{
    if (!DirectionalCascadeSlots.empty())
    {
        return DirectionalCascadeSlots;
    }
    
    DirectionalCascadeSlots.reserve(DirectionalCascadeCount);
    
    for (uint32 CascadeIndex = 0; CascadeIndex < DirectionalCascadeCount; ++CascadeIndex)
    {
        const uint32 GridX = CascadeIndex % DirectionalAtlasGridDimension;
        const uint32 GridY = CascadeIndex / DirectionalAtlasGridDimension;
        
        FDirectionalAtlasSlotDesc Slot = {};
        Slot.CascadeIndex = CascadeIndex;
        Slot.X = GridX * DirectionalCascadeResolution;
        Slot.Y = GridY * DirectionalCascadeResolution;
        Slot.Width = DirectionalCascadeResolution;
        Slot.Height = DirectionalCascadeResolution;
        Slot.AtlasRect = FVector4(
            static_cast<float>(Slot.X) / static_cast<float>(DirectionalAtlasResolution),
            static_cast<float>(Slot.Y) / static_cast<float>(DirectionalAtlasResolution),
            static_cast<float>(Slot.Width) / static_cast<float>(DirectionalAtlasResolution),
            static_cast<float>(Slot.Height) / static_cast<float>(DirectionalAtlasResolution));
        DirectionalCascadeSlots.push_back(Slot);
    }
    
    return DirectionalCascadeSlots;
}
