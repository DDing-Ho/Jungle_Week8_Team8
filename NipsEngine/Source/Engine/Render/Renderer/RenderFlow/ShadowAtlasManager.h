#pragma once

#include "Core/CoreMinimal.h"
#include "Render/Common/ComPtr.h"
#include "Render/Common/RenderTypes.h"

struct FSpotAtlasSlotDesc
{
    uint32 TileIndex = 0;
    uint32 X = 0;
    uint32 Y = 0;
    uint32 Width = 0;
    uint32 Height = 0;
    
    // xy = normalized UV offset, zw = normalized UV scale
    FVector4 AtlasRect = FVector4(0.0f, 0.0f, 1.0f, 1.0f);
};

class FShadowAtlasManager
{
public:
    // Phase 1 Spot atlas:
    // - 4096x4096 depth texture 1장
    // - 1024x1024 고정 타일 16칸(4x4)
    // Directional/Point는 아직 다루지 않고 Spot만 맡습니다.
    static constexpr uint32 SpotAtlasResolution = 4096;
    static constexpr uint32 SpotTileResolution = 1024;
    static constexpr uint32 SpotTilesPerRow = SpotAtlasResolution / SpotTileResolution;
    static constexpr uint32 MaxSpotShadowCount = SpotTilesPerRow * SpotTilesPerRow;
    
public:
    // Spot shadow atlas 텍스처/DSV/SRV를 생성합니다.
    bool Initialize(ID3D11Device* Device);
    void Release();
    
    // Phase 1은 고정 4x4 배치이므로 tile index만으로 바로 atlas rect를 계산합니다.
    static bool BuildFixedSpotSlot(uint32 TileIndex, FSpotAtlasSlotDesc& OutSlot);
    
    ID3D11DepthStencilView* GetSpotAtlasDSV() const { return SpotAtlasDSV.Get(); }
    ID3D11ShaderResourceView* GetSpotAtlasSRV() const { return SpotAtlasSRV.Get(); }
    
private:
    TComPtr<ID3D11Texture2D> SpotAtlasTexture;
    TComPtr<ID3D11DepthStencilView> SpotAtlasDSV;
    TComPtr<ID3D11ShaderResourceView> SpotAtlasSRV;
};
