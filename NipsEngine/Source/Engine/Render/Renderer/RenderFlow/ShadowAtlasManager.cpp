#include "ShadowAtlasManager.h"

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
    // R32_TYPELESS <-> D32_FLOAT 조합이어야 atlas 생성이 성공합니다.
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

void FShadowAtlasManager::Release()
{
    SpotAtlasSRV.Reset();
    SpotAtlasDSV.Reset();
    SpotAtlasTexture.Reset();
}

bool FShadowAtlasManager::BuildFixedSpotSlot(uint32 TileIndex, FSpotAtlasSlotDesc& OutSlot)
{
    if (TileIndex >= MaxSpotShadowCount)
    {
        return false;
    }
    
    const uint32 TileX = TileIndex % SpotTilesPerRow;
    const uint32 TileY = TileIndex / SpotTilesPerRow;
    
    // 한 칸은 atlas 전체가 아니라 1024x1024 tile 하나입니다.
    OutSlot.TileIndex = TileIndex;
    OutSlot.X = TileX * SpotTileResolution;
    OutSlot.Y = TileY * SpotTileResolution;
    OutSlot.Width = SpotTileResolution;
    OutSlot.Height = SpotTileResolution;
    OutSlot.AtlasRect = FVector4(
        static_cast<float>(OutSlot.X) / static_cast<float>(SpotAtlasResolution),
        static_cast<float>(OutSlot.Y) / static_cast<float>(SpotAtlasResolution),
        static_cast<float>(OutSlot.Width) / static_cast<float>(SpotAtlasResolution),
        static_cast<float>(OutSlot.Height) / static_cast<float>(SpotAtlasResolution));

    return true;
}
