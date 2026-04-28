#pragma once
#include "RenderBus.h"
#include "Render/Resource/MeshBufferManager.h"
#include "Spatial/WorldSpatialIndex.h"
#include "Geometry/OBB.h"
#include <unordered_set>

enum class EWorldType : uint32;

class UWorld;
class AActor;
class ASpotLightActor;
class UPrimitiveComponent;
class UGizmoComponent;
class UDecalComponent;
class UFireballComponent;
class FLineBatcher;
struct FFrustum;

class FRenderCollector {
public:
	struct FCullingStats
	{
		int32 TotalVisiblePrimitiveCount{0};
		int32 BVHPassedPrimitiveCount{0};
		int32 FallbackPassedPrimitiveCount{0};
	};

	struct FDecalStats
	{
		int32 TotalDecalCount = 0;
		int32 CollectTimeMS = 0;
	};

	struct FShadowStats
	{
		uint32 DirectionalLightCount = 0;
		uint32 PointLightCount = 0;
		uint32 SpotLightCount = 0;
		uint32 AmbientLightCount = 0;

		uint32 DirectionalShadowCount = 0;
		uint32 PointShadowCount = 0;
		uint32 SpotShadowCount = 0;

		size_t DirectionalShadowMemoryBytes = 0;
		size_t PointShadowMemoryBytes = 0;
		size_t SpotShadowMemoryBytes = 0;

		FDirectionalShadowConstants DirectionalShadowConstants = {};

		size_t GetTotalShadowMemoryBytes() const
		{
			return DirectionalShadowMemoryBytes + PointShadowMemoryBytes + SpotShadowMemoryBytes;
		}
	};

private:
	FMeshBufferManager MeshBufferManager;
	FWorldSpatialIndex::FPrimitiveFrustumQueryScratch FrustumQueryScratch;
	FWorldSpatialIndex::FPrimitiveOBBQueryScratch OBBQueryScratch;
	TArray<UPrimitiveComponent*> VisiblePrimitiveScratch;
	FLineBatcher* LineBatcher = nullptr;
	FCullingStats LastCullingStats;
	FDecalStats LastDecalStats;
	FShadowStats LastShadowStats;

public:
	void Initialize(ID3D11Device* InDevice) { MeshBufferManager.Create(InDevice); }
	void Release() { LineBatcher = nullptr; MeshBufferManager.Release(); }
	void SetLineBatcher(FLineBatcher* InLineBatcher) { LineBatcher = InLineBatcher; }
	void ClearLineBatcher() { LineBatcher = nullptr; }

	void CollectWorld(UWorld* World, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus,
	                  const FFrustum* ViewFrustum = nullptr);
	void CollectSelection(const TArray<AActor*>& SelectedActors, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus);
	void CollectGizmo(UGizmoComponent* Gizmo, const FShowFlags& ShowFlags, FRenderBus& RenderBus, bool bIsActiveOperation);
	void CollectGrid(float GridSpacing, int32 GridHalfLineCount, FRenderBus& RenderBus, bool bOrthographic = false);
	const FCullingStats& GetLastCullingStats() const { return LastCullingStats; }
	const FDecalStats& GetLastDecalStats() const { return LastDecalStats; }
	const FShadowStats& GetLastShadowStats() const { return LastShadowStats; }

private:
	void ResetCullingStats();
	void ResetDecalStats();
	void ResetShadowStats();

	void CollectLight(UWorld* World, FRenderBus& RenderBus, const FFrustum* ViewFrustum = nullptr);
	void CollectFromActor(AActor* Actor, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, EWorldType WorldType);
	bool CollectFromSelectedActor(AActor* Actor, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus);
	void CollectFromComponent(UPrimitiveComponent* Primitive, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, EWorldType WorldType);
	void CollectBVHInternalNodeAABBs(UPrimitiveComponent* PrimitiveComponent, const FShowFlags& ShowFlags, FRenderBus& RenderBus,
	                                 std::unordered_set<int32>& SeenNodeIndices);
};
