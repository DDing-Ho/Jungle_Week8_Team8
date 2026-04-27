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
    static constexpr uint32 SpotAtlasResolution = 4096;
    
    // allocator의 기본 분할 단위
    // 4096 -> 256 -> 16 x 16 셀 그리드
    static constexpr uint32 SpotCellResolution = 256;
    static constexpr uint32 SpotAtlasCellsPerRow = SpotAtlasResolution / SpotCellResolution;
    
    // 하용 타일 크기
    static constexpr uint32 MinSpotTileResolution = 256;
    static constexpr uint32 MaxSpotTileResolution = 2048;
    
    // 이론상 최대 shadow 수
    static constexpr uint32 MaxSpotShadowCount = SpotAtlasCellsPerRow * SpotAtlasCellsPerRow;
    
public:
    // Spot shadow atlas 텍스처/DSV/SRV를 생성합니다.
    bool Initialize(ID3D11Device* Device);
    void Release();
    
    // 프레임 시작 시 dynamic allocation 상태를 초기화
    // 아직 cache X -> 전체 reset
    static void BeginSpotFrame();
    
    // LightComponent의 ShadowResolutionScale 결과를
    // 2048 / 1024 / 512 / 2556 중 하나로 정규화
    static uint32 SnapSpotTileSize(float RequestedResolution);
    
    // 원하는 크기의 spot shadow tule을 atlas 안에서 찾음
    // 성공하면 OutSlot에 atlas 위치/크기 채워서 반환
    static bool RequestSpotSlot(uint32 DesiredResolution, FSpotAtlasSlotDesc& OutSlot);
    
    // Debug overlay가 현재 프레임의 실제 할당 결과를 볼 수 있도록 함
    static const TArray<FSpotAtlasSlotDesc>& GetActiveSpotSlots();
    
    ID3D11DepthStencilView* GetSpotAtlasDSV() const { return SpotAtlasDSV.Get(); }
    ID3D11ShaderResourceView* GetSpotAtlasSRV() const { return SpotAtlasSRV.Get(); }

private:
    // allocator가 실제로 처리 가능한 PoT 타일 크기로 보정합니다.
    // 반환값은 256 / 512 / 1024 / 2048 중 하나입니다.
    static uint32 SanitizeSpotTileSize(uint32 DesiredResolution);
    static bool TryAllocateSpotSlot(uint32 TileResolution, FSpotAtlasSlotDesc& OutSlot);
    static bool IsSpotRegionFree(uint32 CellX, uint32 CellY, uint32 CellSpan);
    static void MarkSpotRegion(uint32 CellX, uint32 CellY, uint32 CellSpan, bool bOccupied);
    static void BuildSpotSlotDesc(uint32 CellX, uint32 CellY, uint32 TileResolution, uint32 TileIndex, FSpotAtlasSlotDesc& OutSlot);

    
private:
    TComPtr<ID3D11Texture2D> SpotAtlasTexture;
    TComPtr<ID3D11DepthStencilView> SpotAtlasDSV;
    TComPtr<ID3D11ShaderResourceView> SpotAtlasSRV;

    // 16x16 cell occupancy map
    // 각 셀은 256x256 영역을 의미함
    static TArray<uint8> SpotCellOccupancy;
    static TArray<FSpotAtlasSlotDesc> ActiveSpotSlots;
};
