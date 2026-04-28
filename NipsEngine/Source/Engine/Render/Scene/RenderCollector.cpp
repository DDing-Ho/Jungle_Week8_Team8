#include "RenderCollector.h"

#include "Render/LineBatcher.h"
#include "Render/Renderer/RenderFlow/ShadowAtlasManager.h"
#include "GameFramework/World.h"
#include "GameFramework/AActor.h"
#include "Object/ActorIterator.h"
#include "Component/BillboardComponent.h"
#include "Component/PrimitiveComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/GizmoComponent.h"
#include "Component/TextRenderComponent.h"
#include "Component/SubUVComponent.h"
#include "Component/DecalComponent.h"
#include "Component/HeightFogComponent.h"
#include "Component/SkyAtmosphereComponent.h"
#include "Component/Light/AmbientLightComponent.h"
#include "Component/Light/DirectionalLightComponent.h"
#include "Component/Light/PointLightComponent.h"
#include "Component/Light/SpotLightComponent.h"
#include "Core/ResourceManager.h"
#include "Engine/Geometry/Frustum.h"
#include "Engine/Asset/StaticMesh.h"
#include "Engine/GameFramework/PrimitiveActors.h"
#include "Render/Resource/Material.h"
#include "Math/Utils.h"
#include "Object/ObjectIterator.h"
#include "Runtime/Stats/ScopeCycleCounter.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <unordered_set>

namespace
{
	// ─────────────────── Constants ───────────────────
	constexpr float SpotShadowNearPlane = 0.1f;
	constexpr float SpotShadowBaseResolution = 1024.0f;
	constexpr size_t ShadowDepthBytesPerPixel = 4;
	constexpr size_t ShadowVSMBytesPerPixel = 8;
	constexpr size_t ShadowBytesPerPixel = ShadowDepthBytesPerPixel + ShadowVSMBytesPerPixel;

	// ─────────────────── Vector ───────────────────
	FVector MakeLightColorVector(const ULightComponentBase* LightComponent);
	FVector MakeStableUpVector(const FVector& Direction);
	FVector4 TransformVector4ByMatrix(const FVector4& Vector, const FMatrix& Matrix);

	// ─────────────────── Shadow ───────────────────
	struct FDirectionalCSMBuildResult
	{
		FMatrix LightViewProj[MAX_CASCADE_COUNT];
		FVector4 SplitDistances;
		FVector4 CascadeRadius;
	};

	struct FDirectionalPSMBuildResult
	{
		FMatrix LightViewProj;
	};

	static_assert(static_cast<uint32>(EShadowMode::CSM) == DirectionalShadowModeValue::CSM);
	static_assert(static_cast<uint32>(EShadowMode::PSM) == DirectionalShadowModeValue::PSM);

	float MakeSpotShadowFarPlane(const USpotLightComponent* SpotLight);
	float MakeSpotShadowResolution(const ULightComponent* LightComponent);
	size_t CalculateShadowTileMemory(uint32 Width, uint32 Height);
	FMatrix MakeSpotShadowViewProjection(const USpotLightComponent* SpotLight, const FVector& LightDirection, float NearPlane, float FarPlane);
	float ComputeSpotShadowPriority(const ULightComponent* LightComponent, const FVector& LightLocation, float AttenuationRadius, const FVector& CameraPosition);
	int32 ExtractActorNumericSuffix(const AActor* Actor);
	FVector InterpolateFrustumCorner(const FVector& NearCorner, const FVector& FarCorner, float NearDepth, float FarDepth, float TargetDepth);
	void CalculatePSSMSplits(int32 CascadeCount, float Lambda, float NearPlane, float ShadowDistance, float* OutSplits);
	void BuildPSMCameraViewProjection(const UDirectionalLightComponent* Light, const FRenderBus& RenderBus, FMatrix& OutView, FMatrix& OutProj);
	bool BuildOrthographicPostProjectiveViewProjection(const FVector& LightDirectionPP, const FVector& CubeCenterPP, float CubeRadiusPP, float MinPlaneGap, FMatrix& OutViewPP, FMatrix& OutProjPP);
	bool BuildDirectionalCSMViewProjection(const UDirectionalLightComponent* Light, const FRenderBus& RenderBus, const FVector& ToLight, FDirectionalCSMBuildResult& OutResult);
	bool BuildDirectionalPSMViewProjection(const UDirectionalLightComponent* Light, const FRenderBus& RenderBus, const FVector& ToLight, FDirectionalPSMBuildResult& OutResult);
	void PackDirectionalCSMShadowConstants(const FDirectionalCSMBuildResult& BuildResult, FDirectionalShadowConstants& OutConstants);
	void PackDirectionalPSMShadowConstants(const FDirectionalPSMBuildResult& BuildResult, FDirectionalShadowConstants& OutConstants);
	struct FSpotShadowCandidate
	{
		FRenderLight RenderLight = {};
		const ULightComponent* LightComponent = nullptr;
		const USpotLightComponent* SpotLight = nullptr;

		FVector LightDirection = FVector::ZeroVector;

		float RequestedResolution = 0.0f;
		uint32 RequestedTileSize = 0;
		float PriorityScore = 0.0f;
	};

	// ─────────────────── Billboard, SubUV ───────────────────
	FMatrix MakeViewBillboardMatrix(const UPrimitiveComponent* Primitive, const FRenderBus& RenderBus);
	FMatrix MakeViewSubUVSelectionMatrix(const USubUVComponent* SubUVComp, const FRenderBus& RenderBus);
	
	// ─────────────────── AABB, BVH ───────────────────
	FColor MakeBVHInternalNodeColor(int32 PathIndexFromLeaf, int32 PathLength);
	bool UsesCameraDependentRenderBounds(const UPrimitiveComponent* PrimitiveComponent);
	FAABB BuildQuadAABB(const FMatrix& WorldMatrix);
	FAABB BuildRenderAABB(const UPrimitiveComponent* PrimitiveComponent, const FRenderBus& RenderBus);
	
	// ─────────────────── LOD ───────────────────
	int32 SelectLODLevel(const FVector& CameraPos, const FAABB& Bounds, const FMatrix& ProjMatrix, int32 ValidLODCount);
}

void FRenderCollector::ResetCullingStats()
{
	LastCullingStats = {};
}

void FRenderCollector::ResetDecalStats()
{
	LastDecalStats = {};
}

void FRenderCollector::ResetShadowStats()
{
	LastShadowStats = {};
}

// 조명을 Frustum Culling을 통해 수집한다.
// Light Collect와 Shadow Collect를 동시에 수행해줍니다.
void FRenderCollector::CollectLight(UWorld* World, FRenderBus& RenderBus, const FFrustum* ViewFrustum)
{
	const TArray<FLightSlot>& LightSlots = World->GetWorldLightSlots();
	int32 Next2DShadowSlice = 0;
	int32 NextSpotShadowIndex = 0;

	// shadow-casting Spot Light 후보를 잠시 모아두는 배열입니다.
	// Spot shadow는 "보이는 순서"가 아니라 "중요한 라이트 순서"로 atlas에 넣어야 하므로,
	// Spot Light를 발견하자마자 바로 할당하지 않고 먼저 후보를 수집합니다.
	TArray<FSpotShadowCandidate> SpotShadowCandidates;
	
	// Spot atlas allocation 상태는 프레임마다 다시 시작함.
	FShadowAtlasManager::BeginSpotFrame();
	
	for (const FLightSlot& Slot : LightSlots)
	{
		if (!Slot.bAlive || !Slot.LightData)
			continue;

		const ULightComponent* LightComponent = Cast<ULightComponent>(Slot.LightData);
		if (LightComponent == nullptr || !LightComponent->IsVisible())
		{
			continue;
		}

		FRenderLight RenderLight = {};
		RenderLight.Type = static_cast<uint32>(LightComponent->GetLightType());
		RenderLight.Color = MakeLightColorVector(LightComponent);
		RenderLight.Intensity = LightComponent->GetIntensity();

		switch (LightComponent->GetLightType())
		{
		case ELightType::LightType_AmbientLight:
		{
			++LastShadowStats.AmbientLightCount;
			RenderBus.AddLight(RenderLight);
			break;
		}

		case ELightType::LightType_Directional:
		{
			++LastShadowStats.DirectionalLightCount;

			FVector Direction = LightComponent->GetForwardVector() * -1.0f; // 빛 방향 벡터
			Direction.Normalize();
			RenderLight.Direction = Direction;

			// 씬의 첫 번째 Directional Light만 그림자를 반영한다.
			if (!RenderBus.HasDirectionalShadow() && LightComponent->IsCastShadows())
			{
				const UDirectionalLightComponent* DirectionalLight = Cast<UDirectionalLightComponent>(LightComponent);
				if (DirectionalLight != nullptr)
				{
					FDirectionalShadowConstants ShadowConstants;
					ShadowConstants.ShadowBias = LightComponent->GetShadowBias();
					ShadowConstants.ShadowSlopeBias = LightComponent->GetShadowSlopeBias();
					ShadowConstants.ShadowSharpen = LightComponent->GetShadowSharpen();
					ShadowConstants.bCascadeDebug = RenderBus.GetShowFlags().bCascadeDebug ? 1 : 0;


					bool bBuiltDirectionalShadow = false;
					switch (DirectionalLight->GetShadowMode())
					{
					case EShadowMode::CSM:
					{
						FDirectionalCSMBuildResult CSMResult = {};
						if (BuildDirectionalCSMViewProjection(DirectionalLight, RenderBus, RenderLight.Direction, CSMResult))
						{
							PackDirectionalCSMShadowConstants(CSMResult, ShadowConstants);
							bBuiltDirectionalShadow = true;
						}
						break;
					}
					case EShadowMode::PSM:
					{
						FDirectionalPSMBuildResult PSMResult = {};
						if (BuildDirectionalPSMViewProjection(DirectionalLight, RenderBus, RenderLight.Direction, PSMResult))
						{
							PackDirectionalPSMShadowConstants(PSMResult, ShadowConstants);
							bBuiltDirectionalShadow = true;
						}
						break;
					}
					default:
						break;
					}

					if (bBuiltDirectionalShadow)
					{
						ShadowConstants.ShadowFilterType = static_cast<uint32>(RenderBus.GetShadowFilterType());
						RenderBus.SetDirectionalShadow(ShadowConstants);
						RenderLight.bCastShadows = 1; // uint32
						LastShadowStats.DirectionalShadowConstants = ShadowConstants;
						LastShadowStats.DirectionalShadowCount = 1;

						const uint32 DirectionalTileCount =
							DirectionalLight->GetShadowMode() == EShadowMode::PSM ? 1u : FShadowAtlasManager::DirectionalCascadeCount;
						LastShadowStats.DirectionalShadowMemoryBytes =
							CalculateShadowTileMemory(
								FShadowAtlasManager::DirectionalCascadeResolution,
								FShadowAtlasManager::DirectionalCascadeResolution) * DirectionalTileCount;
					}
				}
			}

			RenderBus.AddLight(RenderLight);

			break;
		}

		case ELightType::LightType_Point:
		{
			const UPointLightComponent* PointLight = Cast<UPointLightComponent>(LightComponent);
			if (PointLight == nullptr)
			{
				continue;
			}
			
			// View Frustum에 대한 Bounding Sphere 교차 검사
			if (ViewFrustum)
			{
				FVector Center = PointLight->GetWorldLocation();
				float Radius = PointLight->GetAttenuationRadius();
				
				if (!ViewFrustum->IntersectsBoundingSphere(Center, Radius))
					continue;
			}

			++LastShadowStats.PointLightCount;

			RenderLight.Position = PointLight->GetWorldLocation();
			RenderLight.Radius = PointLight->GetAttenuationRadius();
			RenderLight.FalloffExponent = PointLight->GetLightFalloffExponent();
			RenderBus.AddLight(RenderLight);

			// TODO : RenderBus.AddShadowCastLight
			break;
		}

		case ELightType::LightType_Spot:
		{
			const USpotLightComponent* SpotLight = Cast<USpotLightComponent>(LightComponent);
			if (SpotLight == nullptr)
			{
				continue;
			}
			const FVector LightLocation = SpotLight->GetWorldLocation();
			const float Attenuation = SpotLight->GetAttenuationRadius();
			const float InnerAngle = SpotLight->GetInnerConeAngle(); // Degree 단위 주의
			const float OuterAngle = SpotLight->GetOuterConeAngle();

			// -z 축을 forward로 사용
			FVector LightDirection = SpotLight->GetUpVector() * -1.0f;
			LightDirection.Normalize();

			// 원뿔 각도에 따라 줄어든 Bounding Sphere 교차 검사
			if (ViewFrustum)
			{
				const float SpotAngle = MathUtil::Clamp(std::max(OuterAngle, InnerAngle), 0.0f, 89.0f);
				const float SpotRadian = MathUtil::DegreesToRadians(SpotAngle);

				FVector Center = LightLocation;
				float Radius = Attenuation;

				if (SpotAngle <= 45.0f)
				{
					const float SpotRadian = MathUtil::DegreesToRadians(SpotAngle);
					const float Offset = Attenuation * 0.5f;
			
					Center = LightLocation + (LightDirection * Offset);
			
					const float TanAngle = std::tan(SpotRadian);
					const float BaseRadius = Attenuation * TanAngle;

					Radius = std::sqrt((Offset * Offset) + (BaseRadius * BaseRadius));
				}

				if (!ViewFrustum->IntersectsBoundingSphere(Center, Radius))
				{
					continue;
				}
			}

			++LastShadowStats.SpotLightCount;

			RenderLight.Position = LightLocation;
			RenderLight.Direction = LightDirection;
			RenderLight.Radius = Attenuation;
			RenderLight.FalloffExponent = SpotLight->GetLightFalloffExponent();
			RenderLight.SpotInnerCos = std::cos(MathUtil::DegreesToRadians(InnerAngle));
			RenderLight.SpotOuterCos = std::cos(MathUtil::DegreesToRadians(OuterAngle));
			
			if (!LightComponent->IsCastShadows())
			{
				RenderBus.AddLight(RenderLight);
				break;
			}
			
			FSpotShadowCandidate Candidate = {};
			Candidate.RenderLight = RenderLight;
			Candidate.LightComponent = LightComponent;
			Candidate.SpotLight = SpotLight;
			Candidate.LightDirection = LightDirection;

			// 1) 라이트가 원하는 shadow 해상도 계산
			Candidate.RequestedResolution = MakeSpotShadowResolution(LightComponent);

			// 2) allocator가 산정한 PoT 타일 크기로 정규화
			Candidate.RequestedTileSize = FShadowAtlasManager::SnapSpotTileSize(Candidate.RequestedResolution);

			// 3) 후보들에 대해서 priority score 산출
			Candidate.PriorityScore = ComputeSpotShadowPriority(
				LightComponent, LightLocation,
				Attenuation, RenderBus.GetCameraPosition());
			
			SpotShadowCandidates.push_back(Candidate);
			break;
		}
		default:
			break;
		}
	}
	// ----------------------------------------------
	// A) Priority에 따른 atlas 영역 할당 및 downgrade
	// ----------------------------------------------
	// Priority가 높은 라이트부터 atlas에 넣고, 같은 priority라면 큰 타일부터 먼저 넣음.
	std::sort(SpotShadowCandidates.begin(), SpotShadowCandidates.end(),
		[](const FSpotShadowCandidate& A, const FSpotShadowCandidate& B)
		{
			if (std::fabs(A.PriorityScore - B.PriorityScore) > 1.0e-4f)
			{
				return A.PriorityScore > B.PriorityScore;
			}
			if (A.RequestedTileSize != B.RequestedTileSize)
			{
				return A.RequestedTileSize > B.RequestedTileSize;
			}
			return A.RenderLight.Intensity > B.RenderLight.Intensity;
		});
	
	// 정렬된 순서대로 atlas 영역을 배정.
	for (FSpotShadowCandidate& Candidate : SpotShadowCandidates)
	{
		FSpotAtlasSlotDesc SpotSlot = {};
		bool bAllocated = false;
		
		// 1차 시도: 라이트가 원한 타일 크기로 먼저 배정
		uint32 AttemptTileSize = Candidate.RequestedTileSize;
		while (true)
		{
			if (FShadowAtlasManager::RequestSpotSlot(AttemptTileSize, SpotSlot))
			{
				bAllocated = true;
				break;
			}
			
			// 실패하면 절반 크기로 낮춰서 다시 시도
			if (AttemptTileSize <= FShadowAtlasManager::MinSpotTileResolution)
			{
				break;
			}
			
			AttemptTileSize >>= 1u;
			if (AttemptTileSize < FShadowAtlasManager::MinSpotTileResolution)
			{
				AttemptTileSize = FShadowAtlasManager::MinSpotTileResolution;
			}
		}
		
		FRenderLight FinalLight = Candidate.RenderLight;
		
		if (bAllocated)
		{
			const int32 ShadowMapIndex = NextSpotShadowIndex++;
			const float NearPlane = SpotShadowNearPlane;
			const float FarPlane = MakeSpotShadowFarPlane(Candidate.SpotLight);
			const float ShadowBias = Candidate.LightComponent->GetShadowBias();
			const float ShadowSharpen = Candidate.LightComponent->GetShadowSharpen();
			const int32 DebugLightId = ExtractActorNumericSuffix(Candidate.LightComponent->GetOwner());
			
			FinalLight.bCastShadows = 1;
			FinalLight.ShadowMapIndex = ShadowMapIndex;
			FinalLight.ShadowBias = ShadowBias;

			SpotSlot.DebugLightId = DebugLightId;
			FShadowAtlasManager::UpdateSpotSlotDebugLightId(SpotSlot.TileIndex, DebugLightId);
			
			FSpotShadowConstants ShadowData = {};
			ShadowData.LightViewProj = MakeSpotShadowViewProjection(Candidate.SpotLight, Candidate.LightDirection, NearPlane, FarPlane);
			ShadowData.AtlasRect = SpotSlot.AtlasRect;
			
			// 실제 할당된 타일 크기를 넘겨줌
			ShadowData.ShadowResolution = static_cast<float>(SpotSlot.Width);
			ShadowData.ShadowBias = ShadowBias;
			ShadowData.ShadowSharpen = ShadowSharpen;
			
			RenderBus.AddCastShadowSpotLight(ShadowData);
			++LastShadowStats.SpotShadowCount;
			LastShadowStats.SpotShadowMemoryBytes += CalculateShadowTileMemory(SpotSlot.Width, SpotSlot.Height);
		}

		// 끝까지 atlas에 못 들어간 경우에는 shadow만 빠지고 light만 살아있음.
		RenderBus.AddLight(FinalLight);
	}
}

// 조명별 shadow 영향 볼륨으로 BVH를 조회해 shadow caster command를 수집합니다.
void FRenderCollector::CollectShadowCasters(UWorld* World, FRenderBus& RenderBus)
{
	if (World == nullptr)
	{
		return;
	}

	const EWorldType WorldType = World->GetWorldType();
	std::unordered_set<UPrimitiveComponent*> AddedPrimitives;

	auto AddShadowCaster = [&](UPrimitiveComponent* Primitive)
	{
		if (Primitive == nullptr || !Primitive->IsVisible()) return;
		if (Primitive->IsEditorOnly() && WorldType != EWorldType::Editor) return;
		if (Primitive->GetPrimitiveType() != EPrimitiveType::EPT_StaticMesh) return;
		if (!AddedPrimitives.insert(Primitive).second) return;

		UStaticMeshComponent* StaticMeshComp = static_cast<UStaticMeshComponent*>(Primitive);
		const UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh();
		if (StaticMesh == nullptr || !StaticMesh->HasValidMeshData()) return;

		FMeshBuffer* MeshBuffer = MeshBufferManager.GetStaticMeshBuffer(StaticMesh, 0);
		if (MeshBuffer == nullptr || !MeshBuffer->IsValid()) return;

		const FStaticMesh* MeshData = StaticMesh->GetMeshData(0);
		if (MeshData == nullptr) return;

		for (const FStaticMeshSection& Section : MeshData->Sections)
		{
			FRenderCommand Cmd = {};
			Cmd.PerObjectConstants = FPerObjectConstants{ Primitive->GetWorldMatrix(), FColor::White().ToVector4() };
			Cmd.Type = ERenderCommandType::StaticMesh;
			Cmd.MeshBuffer = MeshBuffer;
			Cmd.SectionIndexStart = Section.StartIndex;
			Cmd.SectionIndexCount = Section.IndexCount;

			RenderBus.AddCommand(ERenderPass::ShadowCasters, Cmd);
		}
	};

	auto AddQueryResults = [&]()
	{
		for (UPrimitiveComponent* Primitive : VisiblePrimitiveScratch)
		{
			AddShadowCaster(Primitive);
		}
	};

	if (const FDirectionalShadowConstants* DirectionalShadow = RenderBus.GetDirectionalShadow())
	{
		const int32 DirectionalQueryCount =
			(DirectionalShadow->ShadowMode == DirectionalShadowModeValue::PSM) ? 1 : MAX_CASCADE_COUNT;
		for (int32 CascadeIndex = 0; CascadeIndex < DirectionalQueryCount; ++CascadeIndex)
		{
			FFrustum CascadeFrustum;
			CascadeFrustum.UpdateFromCamera(DirectionalShadow->LightViewProj[CascadeIndex]);
			World->GetSpatialIndex().FrustumQueryPrimitives(CascadeFrustum, VisiblePrimitiveScratch, FrustumQueryScratch);
			AddQueryResults();
		}
	}

	for (const FLightSlot& Slot : World->GetWorldLightSlots())
	{
		const ULightComponent* Light = Cast<ULightComponent>(Slot.LightData);
		if (!Slot.bAlive || Light == nullptr || !Light->IsVisible() || !Light->IsCastShadows()) continue;

		FVector Center = FVector::ZeroVector;
		float Radius = 0.0f;

		if (Light->GetLightType() == ELightType::LightType_Point)
		{
			const UPointLightComponent* PointLight = Cast<UPointLightComponent>(Light);
			if (PointLight == nullptr) continue;

			Center = PointLight->GetWorldLocation();
			Radius = PointLight->GetAttenuationRadius();
		}
		else if (Light->GetLightType() == ELightType::LightType_Spot)
		{
			const USpotLightComponent* SpotLight = Cast<USpotLightComponent>(Light);
			if (SpotLight == nullptr) continue;

			const float SpotAngle = MathUtil::Clamp(std::max(SpotLight->GetOuterConeAngle(), SpotLight->GetInnerConeAngle()), 0.0f, 89.0f);
			Center = SpotLight->GetWorldLocation();
			Radius = SpotLight->GetAttenuationRadius();

			if (SpotAngle <= 45.0f)
			{
				const float Offset = Radius * 0.5f;
				const float BaseRadius = Radius * std::tan(MathUtil::DegreesToRadians(SpotAngle));
				Center += (SpotLight->GetUpVector() * -1.0f).GetSafeNormal() * Offset;
				Radius = std::sqrt((Offset * Offset) + (BaseRadius * BaseRadius));
			}
		}
		else
		{
			continue;
		}

		World->GetSpatialIndex().SphereQueryPrimitives(Center, Radius, VisiblePrimitiveScratch, SphereQueryScratch);
		AddQueryResults();
	}
}

void FRenderCollector::CollectSelection(const TArray<AActor*>& SelectedActors, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus)
{
	bool bHasSelectionMask = false;
	for (AActor* Actor : SelectedActors)
	{
		bHasSelectionMask |= CollectFromSelectedActor(Actor, ShowFlags, ViewMode, RenderBus);
	}

	if (bHasSelectionMask)
	{
		FRenderCommand PostProcessCmd = {};
		PostProcessCmd.Type = ERenderCommandType::PostProcessOutline;
		PostProcessCmd.Material = FResourceManager::Get().GetMaterial("OutlineMaterial");

		UMaterial* Material = Cast<UMaterial>(PostProcessCmd.Material);
		Material->SetVector2("OutlineViewportSize", RenderBus.GetViewportSize());
		Material->SetVector2("OutlineViewportOrigin", RenderBus.GetViewportOrigin());
		Material->DepthStencilType = EDepthStencilType::Default;
		Material->RasterizerType = ERasterizerType::SolidBackCull;
		Material->BlendType = EBlendType::AlphaBlend;

		RenderBus.AddCommand(ERenderPass::PostProcessOutline, PostProcessCmd);
	}
}

void FRenderCollector::CollectGrid(float GridSpacing, int32 GridHalfLineCount, FRenderBus& RenderBus, bool bOrthographic)
{
	FRenderCommand Cmd = {};
	Cmd.Type = ERenderCommandType::Grid;
	Cmd.Constants.Grid.GridSpacing = GridSpacing;
	Cmd.Constants.Grid.GridHalfLineCount = GridHalfLineCount;
	Cmd.Constants.Grid.bOrthographic = bOrthographic;
	RenderBus.AddCommand(ERenderPass::Grid, Cmd);
}

void FRenderCollector::CollectGizmo(UGizmoComponent* Gizmo, const FShowFlags& ShowFlags, FRenderBus& RenderBus, bool bIsActiveOperation)
{
	if (ShowFlags.bGizmo == false) return;
	if (!Gizmo || !Gizmo->IsVisible()) return;

	FMeshBuffer* GizmoMesh = &MeshBufferManager.GetMeshBuffer(Gizmo->GetPrimitiveType());
	FMatrix WorldMatrix = Gizmo->GetWorldMatrix();
	bool bHolding = Gizmo->IsHolding();
	int32 SelectedAxis = Gizmo->GetSelectedAxis();

	auto CreateGizmoCmd = [&](bool bInner) {
		FRenderCommand Cmd = {};
		Cmd.Type = ERenderCommandType::Gizmo;
		Cmd.MeshBuffer = GizmoMesh;

		Cmd.SectionIndexStart = 0;
		Cmd.SectionIndexCount = GizmoMesh->GetIndexBuffer().GetIndexCount();

		Cmd.PerObjectConstants = FPerObjectConstants{ WorldMatrix };

		UMaterial* Material = Cast<UMaterial>(Gizmo->GetMaterial());
		Cmd.Material = Material;

		if (bInner)
		{
			Material->DepthStencilType = EDepthStencilType::GizmoInside;
			Material->BlendType = EBlendType::AlphaBlend;
		}
		else
		{
			Material->DepthStencilType = EDepthStencilType::GizmoOutside;
			Material->BlendType = EBlendType::Opaque;
		}

		Material->SetVector4("GizmoColorTint", FVector4(1.0f, 1.0f, 1.0f, 1.0f));
		Material->SetBool("bIsInnerGizmo", bInner);
		Material->SetBool("bClicking", bHolding);
		Material->SetUInt("SelectedAxis", (SelectedAxis >= 0 && bIsActiveOperation) ? static_cast<uint32>(SelectedAxis) : 0xffffffffu);
		Material->SetFloat("HoveredAxisOpacity", 0.3f);

		return Cmd;
		};

	RenderBus.AddCommand(ERenderPass::DepthLess, CreateGizmoCmd(false));

	if (!bHolding)
	{
		RenderBus.AddCommand(ERenderPass::DepthLess, CreateGizmoCmd(true));
	}
}

void FRenderCollector::CollectFromActor(AActor* Actor, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, EWorldType WorldType)
{
	if (!Actor->IsVisible()) return;

	for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
	{
		CollectFromComponent(Primitive, ShowFlags, ViewMode, RenderBus, WorldType);
	}
}

bool FRenderCollector::CollectFromSelectedActor(AActor* Actor, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus)
{
	if (!Actor->IsVisible()) return false;

	bool bHasSelectionMask = false;
	std::unordered_set<int32> SeenBVHNodeIndices;

	for (UPrimitiveComponent* primitiveComponent : Actor->GetPrimitiveComponents())
	{
		if (!primitiveComponent->IsVisible()) continue;
		if (primitiveComponent->IsEditorOnly())
		{
			UWorld* World = Actor->GetFocusedWorld();
			if (World && World->GetWorldType() != EWorldType::Editor)
				continue;
		}

		FMeshBuffer* MeshBuffer = nullptr;
		if (primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_StaticMesh)
		{
			auto* StaticMeshComp = static_cast<UStaticMeshComponent*>(primitiveComponent);
			MeshBuffer = MeshBufferManager.GetStaticMeshBuffer(StaticMeshComp->GetStaticMesh());
		}
		else
		{
			MeshBuffer = &MeshBufferManager.GetMeshBuffer(primitiveComponent->GetPrimitiveType());
		}

		if (!MeshBuffer)
		{
			continue;
		}

		FRenderCommand BaseCmd{};
		BaseCmd.MeshBuffer = MeshBuffer;
		BaseCmd.PerObjectConstants = FPerObjectConstants(primitiveComponent->GetWorldMatrix());
		BaseCmd.SectionIndexStart = 0;
		BaseCmd.SectionIndexCount = MeshBuffer->GetIndexBuffer().GetIndexCount();

		if (primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_Text)
		{
			UTextRenderComponent* TextComp = static_cast<UTextRenderComponent*>(primitiveComponent);
			const FFontResource* Font = TextComp->GetFont();
			if (!Font || !Font->IsLoaded()) continue;
			const FString& Text = TextComp->GetText();
			if (Text.empty()) continue;

			FMatrix WorldMatrix = TextComp->GetTextMatrix();

			FRenderCommand TextCmd = BaseCmd;
			BaseCmd.PerObjectConstants = FPerObjectConstants(WorldMatrix);
			TextCmd.PerObjectConstants = FPerObjectConstants(TextComp->GetWorldMatrix(), TextComp->GetColor());
			TextCmd.Type = ERenderCommandType::Font;
			TextCmd.Constants.Font.Text = &Text;
			TextCmd.Constants.Font.Font = Font;
			TextCmd.Constants.Font.Scale = TextComp->GetFontSize();
			RenderBus.AddCommand(ERenderPass::Font, TextCmd);
		}
		else if (primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_SubUV)
		{
			BaseCmd.PerObjectConstants.Model = MakeViewSubUVSelectionMatrix(
				static_cast<USubUVComponent*>(primitiveComponent),
				RenderBus);
		}

		else if(primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_Billboard)
		{
			const UBillboardComponent* BComp = static_cast<const UBillboardComponent*>(primitiveComponent);
			BaseCmd.PerObjectConstants.Model = UBillboardComponent::MakeBillboardWorldMatrix(
				BComp->GetWorldLocation(),
				FVector(0.01f, BComp->GetWidth(), BComp->GetHeight()),
				RenderBus.GetCameraForward(),
				RenderBus.GetCameraRight(),
				RenderBus.GetCameraUp());
		}

		if (!primitiveComponent->SupportsOutline()) continue;

		// Selection Mask
		FRenderCommand MaskCmd = BaseCmd;
		if (primitiveComponent->GetPrimitiveType() == EPrimitiveType::EPT_Billboard)
		{
			MaskCmd.Type = ERenderCommandType::BillboardSelectionMask;
			MaskCmd.Constants.Billboard.Texture = static_cast<UBillboardComponent*>(primitiveComponent)->GetTexture();
		}
		else
		{
			MaskCmd.Type = ERenderCommandType::SelectionMask;
		}
		RenderBus.AddCommand(ERenderPass::SelectionMask, MaskCmd);
		bHasSelectionMask = true;

		if (ShowFlags.bBoundingVolume)
		{
			LineBatcher->AddAABB(BuildRenderAABB(primitiveComponent, RenderBus), FColor::White());
		}

		CollectBVHInternalNodeAABBs(primitiveComponent, ShowFlags, RenderBus, SeenBVHNodeIndices);
	}

	// 선택된 Light Components의 Bounding 시각화
	for (UActorComponent* Component : Actor->GetComponents())
	{
		const ULightComponent* LightComponent = Cast<ULightComponent>(Component);
		if (LightComponent == nullptr || !LightComponent->IsVisible())
		{
			continue;
		}

		switch (LightComponent->GetLightType())
		{
		case ELightType::LightType_Directional:
		{
			const UDirectionalLightComponent* Light = Cast<UDirectionalLightComponent>(LightComponent);
			LineBatcher->AddDirectionalLight(Light->GetWorldLocation(), Light->GetForwardVector(), Light->GetRightVector(), Light->GetLightColor().ToVector4());
			break;
		}
		case ELightType::LightType_AmbientLight:
		{
			break;
		}

		case ELightType::LightType_Point:
		{
			const UPointLightComponent* Light = Cast<UPointLightComponent>(LightComponent);
			LineBatcher->AddPointLight(Light->GetWorldLocation(), Light->GetAttenuationRadius(), Light->GetRightVector(), Light->GetUpVector());
			break;
		}

		case ELightType::LightType_Spot:
		{
			const USpotLightComponent* Light = Cast<USpotLightComponent>(LightComponent);
			LineBatcher->AddSpotLight(
				Light->GetWorldLocation(),
				Light->GetUpVector() * -1.0f,
				Light->GetRightVector() * -1.0f,
				Light->GetAttenuationRadius(),
				Light->GetInnerConeAngle(),
				Light->GetOuterConeAngle()
			);
			break;
		}
		}
	}

	return bHasSelectionMask;
}

void FRenderCollector::CollectFromComponent(UPrimitiveComponent* Primitive, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus, EWorldType WorldType)
{
	if (!Primitive->IsVisible()) return;
	if (Primitive->IsEditorOnly() && WorldType != EWorldType::Editor) return;

	EPrimitiveType PrimType = Primitive->GetPrimitiveType();

	ID3D11ShaderResourceView* DefaultSRV = FResourceManager::Get().GetDefaultWhiteSRV();
	auto ResolveSRV = [&](const FString& Path) -> ID3D11ShaderResourceView*
		{
			UTexture* Texture = FResourceManager::Get().GetTexture(Path);
			return Texture ? Texture->GetSRV() : DefaultSRV;
		};
	static const FMaterial EngineDefaultMaterial{};

	switch (PrimType)
	{
	case EPrimitiveType::EPT_StaticMesh:
	{
		if (!ShowFlags.bPrimitives) return;

		UStaticMeshComponent* StaticMeshComp = static_cast<UStaticMeshComponent*>(Primitive);
		const UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh();

		if (!StaticMesh || !StaticMesh->HasValidMeshData()) return;

		// 1. 카메라 정보 및 AABB 가져오기
		FVector CameraPos = RenderBus.GetCameraPosition();
		FMatrix ProjMatrix = RenderBus.GetProj();
		FAABB Bounds = StaticMeshComp->GetWorldAABB();
		const int32 ValidLODCount = StaticMesh->GetValidLODCount();

		// 2. LOD 레벨 계산
		int32 SelectedLOD = 0; // 기본값은 항상 원본(최고 화질)
		if (ShowFlags.bEnableLOD)
		{
			SelectedLOD = SelectLODLevel(CameraPos, Bounds, ProjMatrix, ValidLODCount);
		}

		FMeshBuffer* MeshBuffer = MeshBufferManager.GetStaticMeshBuffer(StaticMesh, SelectedLOD);
		if (!MeshBuffer) return;

		const FStaticMesh* MeshData = StaticMesh->GetMeshData(SelectedLOD);
		const TArray<FStaticMeshSection>& Sections = MeshData->Sections;

		for (int32 SectionIdx = 0; SectionIdx < static_cast<int32>(Sections.size()); ++SectionIdx)
		{
			const FStaticMeshSection& Section = Sections[SectionIdx];
			UMaterialInterface* Material = Cast<UMaterialInterface>(StaticMeshComp->GetMaterial(SectionIdx));
			if (Material == nullptr)
			{
				Material = FResourceManager::Get().GetMaterial("DefaultWhite");
				if (Material == nullptr)
				{
					continue;
				}
			}

			FRenderCommand Cmd = {};
			Cmd.PerObjectConstants = FPerObjectConstants{ Primitive->GetWorldMatrix(), FColor::White().ToVector4() };
			Cmd.Type = ERenderCommandType::StaticMesh;
			Cmd.MeshBuffer = MeshBuffer;

			Cmd.SectionIndexStart = Section.StartIndex;
			Cmd.SectionIndexCount = Section.IndexCount;
			Cmd.Material = Material;

			//if (Material)
			//{
			//	Material->SetVector2("ScrollUV", FVector2(StaticMeshComp->GetScroll().first, StaticMeshComp->GetScroll().second));
			//}

			RenderBus.AddCommand(ERenderPass::Opaque, Cmd);

			if (Material->GetEffectiveLightingModel() == ELightingModel::Toon)
			{
				FRenderCommand OutlineCmd = {};
				OutlineCmd.Type = ERenderCommandType::ToonOutline;
				OutlineCmd.MeshBuffer = MeshBuffer;
				OutlineCmd.PerObjectConstants = FPerObjectConstants{
					Primitive->GetWorldMatrix()
				};
				OutlineCmd.SectionIndexStart = Section.StartIndex;
				OutlineCmd.SectionIndexCount = Section.IndexCount;
				OutlineCmd.Material = Material;

				RenderBus.AddCommand(ERenderPass::ToonOutline, OutlineCmd);
			}
		}

		break;
	}

	case EPrimitiveType::EPT_Text:
	{
		if (!ShowFlags.bBillboardText) return;

		UTextRenderComponent* TextComp = static_cast<UTextRenderComponent*>(Primitive);
		const FFontResource* Font = TextComp->GetFont();
		if (!Font || !Font->IsLoaded()) return;

		const FString& Text = TextComp->GetText();
		if (Text.empty()) return;

		FRenderCommand Cmd = {};
		Cmd.Type = ERenderCommandType::Font;
		Cmd.PerObjectConstants = FPerObjectConstants{TextComp->GetWorldMatrix(), TextComp->GetColor()};
		Cmd.Constants.Font.Text = &Text;
		Cmd.Constants.Font.Font = Font;
		Cmd.Constants.Font.Scale = TextComp->GetFontSize();
		
		RenderBus.AddCommand(ERenderPass::Font, Cmd);
		break;
	}

	case EPrimitiveType::EPT_SubUV:
	{
		USubUVComponent* SubUVComp = static_cast<USubUVComponent*>(Primitive);
		const FParticleResource* Particle = SubUVComp->GetParticle();
		if (!Particle || !Particle->IsLoaded()) return;

		FRenderCommand Cmd = {};
		Cmd.PerObjectConstants = FPerObjectConstants{
			MakeViewBillboardMatrix(Primitive, RenderBus),
			FColor::White().ToVector4() };
		Cmd.Type = ERenderCommandType::SubUV;
		Cmd.Constants.SubUV.Particle = Particle;
		Cmd.Constants.SubUV.FrameIndex = SubUVComp->GetFrameIndex();
		Cmd.Constants.SubUV.Width = SubUVComp->GetWidth();
		Cmd.Constants.SubUV.Height = SubUVComp->GetHeight();

		RenderBus.AddCommand(ERenderPass::SubUV, Cmd);
		break;
	}

	case EPrimitiveType::EPT_Billboard:
	{
		UBillboardComponent* BillboardComp = static_cast<UBillboardComponent*>(Primitive);
		UTexture* Texture = BillboardComp->GetTexture();

		UMaterial* BillboardMat = FResourceManager::Get().GetMaterial("BillboardMat");
		BillboardMat->DepthStencilType = EDepthStencilType::Default;
		BillboardMat->BlendType        = EBlendType::AlphaBlend;
		BillboardMat->RasterizerType   = ERasterizerType::SolidNoCull;
		BillboardMat->SamplerType      = ESamplerType::EST_Linear;

		const FMatrix BillboardMatrix = UBillboardComponent::MakeBillboardWorldMatrix(
			BillboardComp->GetWorldLocation(),
			FVector(0.01f, BillboardComp->GetWidth(), BillboardComp->GetHeight()),
			RenderBus.GetCameraForward(),
			RenderBus.GetCameraRight(),
			RenderBus.GetCameraUp());

		// LightComponents에 부착된 빌보드의 색상 조정
		FVector4 LightColor = FColor::White().ToVector4();
		if (ULightComponent* LightComponent = Cast<ULightComponent>(BillboardComp->GetParent()))
		{
			LightColor = LightComponent->GetLightColor().ToVector4();
		}

		FRenderCommand Cmd = {};
		Cmd.Type = ERenderCommandType::Billboard;
		Cmd.MeshBuffer = &MeshBufferManager.GetMeshBuffer(EPrimitiveType::EPT_Billboard);
		Cmd.PerObjectConstants = FPerObjectConstants{ BillboardMatrix, LightColor };
		Cmd.Material = BillboardMat;
		Cmd.Constants.Billboard.Texture = Texture;

		RenderBus.AddCommand(ERenderPass::Billboard, Cmd);
		break;
	}

	case EPrimitiveType::EPT_Decal:
	{
		if (!ShowFlags.bDecals) return;

		FScopeCycleCounter RenderDecalScope({});

		UDecalComponent* DecalComp = static_cast<UDecalComponent*>(Primitive);
		UMaterialInterface* Material = Cast<UMaterialInterface>(DecalComp->GetMaterial());

		UWorld* World = DecalComp->GetOwner() ? DecalComp->GetOwner()->GetFocusedWorld() : nullptr;

		FOBB DecalOBB = FOBB::FromAABB(DecalComp->GetWorldAABB(), DecalComp->GetWorldMatrix());

		TArray<UPrimitiveComponent*> VisiblePrimitiveScratch;
		World->GetSpatialIndex().OBBQueryPrimitives(DecalOBB, VisiblePrimitiveScratch, OBBQueryScratch);

		for (UPrimitiveComponent* Prim : VisiblePrimitiveScratch)
		{
			if (Prim->GetPrimitiveType() != EPrimitiveType::EPT_StaticMesh) continue;

			UStaticMeshComponent* StaticMeshComp = static_cast<UStaticMeshComponent*>(Prim);
			const UStaticMesh* StaticMesh = StaticMeshComp->GetStaticMesh();

			if (!StaticMesh || !StaticMesh->HasValidMeshData()) continue;

			// 1. 카메라 정보 및 AABB 가져오기
			FVector CameraPos = RenderBus.GetCameraPosition();
			FMatrix ProjMatrix = RenderBus.GetProj();
			FAABB Bounds = StaticMeshComp->GetWorldAABB();
			const int32 ValidLODCount = StaticMesh->GetValidLODCount();

			int32 SelectedLOD = 0; // 기본값은 항상 원본(최고 화질)
			if (ShowFlags.bEnableLOD)
			{
				SelectedLOD = SelectLODLevel(CameraPos, Bounds, ProjMatrix, ValidLODCount);
			}

			FMeshBuffer* MeshBuffer = MeshBufferManager.GetStaticMeshBuffer(StaticMesh, SelectedLOD);
			if (!MeshBuffer) return;

			const FStaticMesh* MeshData = StaticMesh->GetMeshData(SelectedLOD);
			const TArray<FStaticMeshSection>& Sections = MeshData->Sections;

			for (int32 SectionIdx = 0; SectionIdx < static_cast<int32>(Sections.size()); ++SectionIdx)
			{
				const FStaticMeshSection& Section = Sections[SectionIdx];

				FRenderCommand Cmd = {};
				Cmd.Type = ERenderCommandType::Decal;
				Cmd.PerObjectConstants = FPerObjectConstants{ Prim->GetWorldMatrix(), DecalComp->GetDecalColor().ToVector4() };
				Cmd.MeshBuffer = MeshBuffer;

				Cmd.SectionIndexStart = Section.StartIndex;
				Cmd.SectionIndexCount = Section.IndexCount;

				Cmd.Material = Material;
				Cmd.Constants.Decal.InvDecalWorld = DecalComp->GetDecalMatrix().GetInverse();

				RenderBus.AddCommand(ERenderPass::Decal, Cmd);
			}
		}

		if (WorldType == EWorldType::Editor)
		{
			LineBatcher->AddOBB(DecalOBB, FColor::Green());
		}

		LastDecalStats.TotalDecalCount += 1;
		LastDecalStats.CollectTimeMS += static_cast<int32>(RenderDecalScope.Finish());
		break;
	}

	case EPrimitiveType::EPT_FOG:
	{
		if (!ShowFlags.bFog)
			return;
		UHeightFogComponent* HeightFogComp = static_cast<UHeightFogComponent*>(Primitive);

		FRenderCommand Cmd = {};
		Cmd.Type = ERenderCommandType::Primitive;
		Cmd.Constants.Fog.FogDensity = HeightFogComp->GetFogDensity();
		Cmd.Constants.Fog.FogColor = HeightFogComp->GetFogInscatteringColor();
		Cmd.Constants.Fog.HeightFalloff = HeightFogComp->GetHeightFalloff();
		Cmd.Constants.Fog.FogHeight = HeightFogComp->GetFogHeight();
		Cmd.Constants.Fog.FogStartDistance = HeightFogComp->GetFogStartDistance();
		Cmd.Constants.Fog.FogMaxOpacity = HeightFogComp->GetFogMaxOpacity();
		Cmd.Constants.Fog.FogCutoffDistance = HeightFogComp->GetFogCutoffDistance();
		//Cmd.BlendState = EBlendState::AlphaBlend;
		//Cmd.DepthStencilState = EDepthStencilState::Default;

		RenderBus.AddCommand(ERenderPass::Fog, Cmd);
		break;
	}
	case EPrimitiveType::EPT_SKY:
	{
		if (!RenderBus.GetCommands(ERenderPass::Sky).empty())
		{
			return;
		}

		USkyAtmosphereComponent* SkyComponent = static_cast<USkyAtmosphereComponent*>(Primitive);
		SkyComponent->RefreshSkyStateFromWorld();

		FRenderCommand Cmd = {};
		Cmd.Type = ERenderCommandType::Sky;
		SkyComponent->FillSkyConstants(RenderBus, Cmd.Constants.Sky);
		RenderBus.AddCommand(ERenderPass::Sky, Cmd);
		break;
	}
	default:
		if (PrimType == EPrimitiveType::EPT_TransGizmo || PrimType == EPrimitiveType::EPT_RotGizmo || PrimType == EPrimitiveType::EPT_ScaleGizmo)
		{
			return;
		}
		return;
	}
}

void FRenderCollector::CollectBVHInternalNodeAABBs(UPrimitiveComponent* PrimitiveComponent, const FShowFlags& ShowFlags, FRenderBus& RenderBus, std::unordered_set<int32>& SeenNodeIndices)
{
	if (!ShowFlags.bBoundingVolume || !ShowFlags.bBVHBoundingVolume || PrimitiveComponent == nullptr || LineBatcher == nullptr)
	{
		return;
	}

	AActor* Owner = PrimitiveComponent->GetOwner();
	UWorld* World = Owner ? Owner->GetFocusedWorld() : nullptr;
	if (World == nullptr)
	{
		return;
	}

	const FWorldSpatialIndex& SpatialIndex = World->GetSpatialIndex();
	const int32 ObjectIndex = SpatialIndex.FindObjectIndex(PrimitiveComponent);
	if (ObjectIndex == FBVH::INDEX_NONE)
	{
		return;
	}

	const FBVH& BVH = SpatialIndex.GetBVH();
	const TArray<int32>& ObjectToLeafNode = BVH.GetObjectToLeafNode();
	if (ObjectIndex < 0 || ObjectIndex >= static_cast<int32>(ObjectToLeafNode.size()))
	{
		return;
	}

	const int32 LeafNodeIndex = ObjectToLeafNode[ObjectIndex];
	if (LeafNodeIndex == FBVH::INDEX_NONE)
	{
		return;
	}

	const TArray<FBVH::FNode>& Nodes = BVH.GetNodes();
	if (LeafNodeIndex < 0 || LeafNodeIndex >= static_cast<int32>(Nodes.size()))
	{
		return;
	}

	TArray<int32> PathToRoot;
	PathToRoot.reserve(16);

	int32 CurrentNodeIndex = Nodes[LeafNodeIndex].Parent;
	while (CurrentNodeIndex != FBVH::INDEX_NONE)
	{
		if (CurrentNodeIndex < 0 || CurrentNodeIndex >= static_cast<int32>(Nodes.size()))
		{
			break;
		}

		PathToRoot.push_back(CurrentNodeIndex);
		CurrentNodeIndex = Nodes[CurrentNodeIndex].Parent;
	}

	for (int32 PathIndex = 0; PathIndex < static_cast<int32>(PathToRoot.size()); ++PathIndex)
	{
		const int32 NodeIndex = PathToRoot[PathIndex];
		if (!SeenNodeIndices.insert(NodeIndex).second)
		{
			continue;
		}

		const FBVH::FNode& Node = Nodes[NodeIndex];
		if (Node.IsLeaf())
		{
			continue;
		}

		const FColor Color = MakeBVHInternalNodeColor(PathIndex, static_cast<int32>(PathToRoot.size()));
		LineBatcher->AddAABB(Node.Bounds, Color);
	}
}

void FRenderCollector::CollectWorld(UWorld* World, const FShowFlags& ShowFlags, EViewMode ViewMode, FRenderBus& RenderBus,
	const FFrustum* ViewFrustum)
{
	ResetCullingStats();
	ResetDecalStats();
	ResetShadowStats();

	if (!World) return;
	
	CollectLight(World, RenderBus, ViewFrustum);
	CollectShadowCasters(World, RenderBus);

	if (ViewFrustum)
	{
		VisiblePrimitiveScratch.clear();
		World->GetSpatialIndex().FrustumQueryPrimitives(*ViewFrustum, VisiblePrimitiveScratch, FrustumQueryScratch);

		for (UPrimitiveComponent* Primitive : VisiblePrimitiveScratch)
		{
			if (Primitive == nullptr || UsesCameraDependentRenderBounds(Primitive) || !Primitive->IsEnableCull()) continue;
			++LastCullingStats.BVHPassedPrimitiveCount;
			CollectFromComponent(Primitive, ShowFlags, ViewMode, RenderBus, World->GetWorldType());
		}
	}

	std::unordered_set<UPrimitiveComponent*> CollectCameraDependentPrimitives;
	if (ViewFrustum)
	{
		CollectCameraDependentPrimitives.reserve(32);
	}
	
	// Frustum이 없다면 액터 단위로 통째로 수집하고, 그렇지 않다면 BVH에서 누락된 컴포넌트들을 개별 수집
	for (TActorIterator<AActor> Iter(World); Iter; ++Iter)
	{
		AActor* Actor = *Iter;
		if (!Actor || !Actor->IsVisible()) continue;

		if (!ViewFrustum)
		{
			for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
			{	
				if (Primitive != nullptr && !Primitive->IsVisible())
				{
					++LastCullingStats.TotalVisiblePrimitiveCount;
				}
			}
			CollectFromActor(Actor, ShowFlags, ViewMode, RenderBus, World->GetWorldType());
			continue; // early-continue
		}

		// 이미 처리된 컴포넌트, 중복된 컴포넌트는 제외하고 Frustum Culling 수행
		for (UPrimitiveComponent* Primitive : Actor->GetPrimitiveComponents())
		{
			if (!Primitive || !Primitive->IsVisible()) continue;

			++LastCullingStats.TotalVisiblePrimitiveCount;

			const bool bIsCameraDependent = UsesCameraDependentRenderBounds(Primitive);
			if (!bIsCameraDependent && Primitive->IsEnableCull()) continue;
			if (!CollectCameraDependentPrimitives.insert(Primitive).second) continue;

			if (bIsCameraDependent && Primitive->IsEnableCull())
			{
				if (ViewFrustum->Intersects(BuildRenderAABB(Primitive, RenderBus)) == FFrustum::EFrustumIntersectResult::Outside)
					continue;
			}

			++LastCullingStats.FallbackPassedPrimitiveCount;
			CollectFromComponent(Primitive, ShowFlags, ViewMode, RenderBus, World->GetWorldType());
		}
	}
}

// ─────────────────── namespace ────────────────────────────────────────────────────────────

namespace
{
	FVector MakeLightColorVector(const ULightComponentBase* LightComponent)
	{
		if (LightComponent == nullptr)
		{
			return FVector::ZeroVector;
		}

		const FColor& LightColor = LightComponent->GetLightColor();
		return FVector(LightColor.r, LightColor.g, LightColor.b);
	}

	FVector MakeStableUpVector(const FVector& Direction)
	{
		const FVector NormalizedDirection = Direction.GetSafeNormal();
		FVector Up = FVector::UpVector;
		if (std::fabs(FVector::DotProduct(Up, NormalizedDirection)) > 0.98f)
		{
			Up = FVector::RightVector;
		}
		return Up;
	}

	FVector4 TransformVector4ByMatrix(const FVector4& Vector, const FMatrix& Matrix)
	{
		return Matrix.TransformVector4(Vector, Matrix);
	}

	float MakeSpotShadowFarPlane(const USpotLightComponent* SpotLight)
	{
		return std::max(SpotLight->GetAttenuationRadius(), SpotShadowNearPlane + 1.0f);
	}

	float MakeSpotShadowResolution(const ULightComponent* LightComponent)
	{
		return std::max(1.0f, SpotShadowBaseResolution * LightComponent->GetShadowResolutionScale());
	}

	size_t CalculateShadowTileMemory(uint32 Width, uint32 Height)
	{
		return static_cast<size_t>(Width) * static_cast<size_t>(Height) * ShadowBytesPerPixel;
	}

	FMatrix MakeSpotShadowViewProjection(
		const USpotLightComponent* SpotLight,
		const FVector& LightDirection,
		float NearPlane,
		float FarPlane)
	{
		const FVector Direction = LightDirection.GetSafeNormal();
		const FVector LightPosition = SpotLight->GetWorldLocation();
		const float FovY = MathUtil::DegreesToRadians(
			MathUtil::Clamp(SpotLight->GetOuterConeAngle() * 2.0f, 1.0f, 175.0f));

		const FMatrix LightView = FMatrix::MakeViewLookAtLH(
			LightPosition,
			LightPosition + Direction,
			MakeStableUpVector(Direction));
		const FMatrix LightProjection = FMatrix::MakePerspectiveFovLH(FovY, 1.0f, NearPlane, FarPlane);
		return LightView * LightProjection;
	}
	
	/* 밝을수록/반경이 클수록/카메라에 가까울수록 점수를 크게 주도록 합니다. */
	float ComputeSpotShadowPriority(
		const ULightComponent* LightComponent,
		const FVector& LightLocation,
		float AttenuationRadius,
		const FVector& CameraPosition)
	{
		const FVector ToCamera = LightLocation - CameraPosition;
		const float DistanceSq = std::max(FVector::DotProduct(ToCamera, ToCamera), 1.0f);
		
		const float ScreenCoverage = std::clamp((AttenuationRadius * AttenuationRadius) / DistanceSq, 0.05f, 8.0f);
	
		return std::max(LightComponent->GetIntensity(), 0.0f) * ScreenCoverage;
	}

	int32 ExtractActorNumericSuffix(const AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return -1;
		}

		const FString ActorName = Actor->GetFName().ToString();
		const size_t UnderscorePos = ActorName.find_last_of('_');
		if (UnderscorePos == FString::npos || UnderscorePos + 1 >= ActorName.size())
		{
			return -1;
		}

		int32 Value = 0;
		bool bHasDigit = false;
		for (size_t Index = UnderscorePos + 1; Index < ActorName.size(); ++Index)
		{
			const unsigned char Ch = static_cast<unsigned char>(ActorName[Index]);
			if (!std::isdigit(Ch))
			{
				return -1;
			}

			bHasDigit = true;
			Value = (Value * 10) + static_cast<int32>(Ch - '0');
		}

		return bHasDigit ? Value : -1;
	}

	/* Frustum 근평면, 원평면 꼭짓점 기준으로 비례식을 세워서 TargetDepth 깊이의 절단면의 꼭짓점을 찾습니다. */
	FVector InterpolateFrustumCorner(
		const FVector& NearCorner,
		const FVector& FarCorner,
		float NearDepth,
		float FarDepth,
		float TargetDepth)
	{
		const float Range = FarDepth - NearDepth;
		if (std::fabs(Range) <= MathUtil::KindaSmallNumber)
		{
			return FarCorner;
		}

		const float T = (TargetDepth - NearDepth) / Range;
		return NearCorner + (FarCorner - NearCorner) * T;
	}

	/* PSSM(Parallel - Split Shadow Map) 공식에 따라 View Frustum을 평행 분할합니다.
	 * Lambda[0, 1] → Linear : 0.0f, Logarithmic : 1.0f */
	void CalculatePSSMSplits(int32 CascadeCount, float Lambda, float NearPlane, float ShadowDistance, float* OutSplits)
	{
		OutSplits[0] = NearPlane;
		for (int32 i = 1; i < CascadeCount; ++i)
		{
			float Fraction = static_cast<float>(i) / CascadeCount; // 전체 Cascade 구간 중 경계선
			float LogarithmSplit = std::pow(ShadowDistance / NearPlane, Fraction);
			float UniformSplit = NearPlane + (ShadowDistance - NearPlane) * Fraction;
			OutSplits[i] =  Lambda * LogarithmSplit + (1.0f - Lambda) * UniformSplit;
		}
		OutSplits[CascadeCount] = ShadowDistance;
	}

	void BuildPSMCameraViewProjection(
		const UDirectionalLightComponent* Light,
		const FRenderBus& RenderBus,
		FMatrix& OutView,
		FMatrix& OutProj)
	{
		OutView = RenderBus.GetView();
		OutProj = RenderBus.GetProj();

		const float VirtualSlideBack = Light != nullptr ? Light->GetPSMVirtualSlideBack() : 0.0f;
		if (VirtualSlideBack <= MathUtil::KindaSmallNumber || RenderBus.IsOrthographic())
		{
			return;
		}

		const FVector CameraForward = RenderBus.GetCameraForward().GetSafeNormal();
		if (CameraForward.IsNearlyZero())
		{
			return;
		}

		FVector CameraUp = RenderBus.GetCameraUp().GetSafeNormal();
		if (CameraUp.IsNearlyZero())
		{
			CameraUp = MakeStableUpVector(CameraForward);
		}

		const float XScale = OutProj.M[1][0];
		const float YScale = OutProj.M[2][1];
		if (std::fabs(XScale) <= MathUtil::KindaSmallNumber ||
			std::fabs(YScale) <= MathUtil::KindaSmallNumber)
		{
			return;
		}

		const FVector VirtualCameraPosition = RenderBus.GetCameraPosition() - CameraForward * VirtualSlideBack;
		const float FovY = 2.0f * std::atan(1.0f / std::fabs(YScale));
		const float AspectRatio = std::fabs(YScale / XScale);
		const float NearPlane = std::max(RenderBus.GetNear() + VirtualSlideBack, 0.001f);
		const float FarPlane = std::max(RenderBus.GetFar(), NearPlane + 0.001f);

		OutView = FMatrix::MakeViewLookAtLH(
			VirtualCameraPosition,
			VirtualCameraPosition + CameraForward,
			CameraUp);
		OutProj = FMatrix::MakePerspectiveFovLH(FovY, AspectRatio, NearPlane, FarPlane);
	}

	bool BuildOrthographicPostProjectiveViewProjection(
		const FVector& LightDirectionPP,
		const FVector& CubeCenterPP,
		float CubeRadiusPP,
		float MinPlaneGap,
		FMatrix& OutViewPP,
		FMatrix& OutProjPP)
	{
		FVector NormalizedLightDirectionPP = LightDirectionPP;
		if (!NormalizedLightDirectionPP.Normalize())
		{
			return false;
		}

		const FVector LightPositionPP = CubeCenterPP + NormalizedLightDirectionPP * (2.0f * CubeRadiusPP);
		const FVector ViewDirectionPP = (CubeCenterPP - LightPositionPP).GetSafeNormal();
		if (ViewDirectionPP.IsNearlyZero())
		{
			return false;
		}

		const float DistToCenter = FVector::Dist(LightPositionPP, CubeCenterPP);
		const float NearPP = std::max(MinPlaneGap, DistToCenter - CubeRadiusPP);
		const float FarPP = std::max(NearPP + MinPlaneGap, DistToCenter + CubeRadiusPP);

		OutViewPP = FMatrix::MakeViewLookAtLH(LightPositionPP, CubeCenterPP, MakeStableUpVector(ViewDirectionPP));
		OutProjPP = FMatrix::MakeOrthographicLH(CubeRadiusPP * 2.0f, CubeRadiusPP * 2.0f, NearPP, FarPP);
		return true;
	}

	/* View Frustum을 PSSM 공식에 따라 평행하게 잘라서 Cascade 구간으로 나눕니다.
	 * 이후 각 구간을 포함하는 최소 크기의 Bounding Sphere를 구하고,
	 * 빛의 시점에서 Bounding Sphere를 덮는 직교 투영 행렬과 뷰 행렬을 생성합니다. */
	bool BuildDirectionalCSMViewProjection(
		const UDirectionalLightComponent* Light,
		const FRenderBus& RenderBus, 
		const FVector& ToLight,
		FDirectionalCSMBuildResult& OutResult)
	{
		constexpr int32 CascadeCount = MAX_CASCADE_COUNT;
		const float NearPlane = std::max(RenderBus.GetNear(), 1.0f);
		const float Lambda = Light->GetCascadeSplitWeight();
		const float ShadowDistance = Light->GetShadowDistance();

		float Splits[MAX_CASCADE_COUNT + 1]; // CascadeCount + 1
		CalculatePSSMSplits(CascadeCount, Lambda, NearPlane, ShadowDistance, Splits);

		const FMatrix InverseViewProjection = (RenderBus.GetView() * RenderBus.GetProj()).GetInverse();
		static constexpr float NdcX[4] = { -1.0f, 1.0f, 1.0f, -1.0f };
		static constexpr float NdcY[4] = { -1.0f, -1.0f, 1.0f, 1.0f };

		FVector NearCorners[4];
		FVector FarCorners[4];
		for (int32 i = 0; i < 4; ++i) // i: Corner Index
		{
			NearCorners[i] = InverseViewProjection.TransformPosition(FVector(NdcX[i], NdcY[i], 0.0f));
			FarCorners[i] = InverseViewProjection.TransformPosition(FVector(NdcX[i], NdcY[i], 1.0f));
		}

		const FVector& CameraPosition = RenderBus.GetCameraPosition();
		const FVector& CameraForward = RenderBus.GetCameraForward();
		const float NearDepth = FVector::DotProduct(NearCorners[0] - CameraPosition, CameraForward);
		const float FarDepth = FVector::DotProduct(FarCorners[0] - CameraPosition, CameraForward);
		const FVector LightDirection = (ToLight * -1.0f).GetSafeNormal();

		for (int32 i = 0; i < CascadeCount; ++i) // i: Cascade Index
		{
			const float CascadeNear = Splits[i];
			const float CascadeFar = Splits[i + 1];
			OutResult.SplitDistances.XYZW[i] = CascadeFar;

			FVector CascadeCorners[8];
			for (int32 j = 0; j < 4; ++j) // j: Corner Index
			{
				CascadeCorners[j] = InterpolateFrustumCorner(NearCorners[j], FarCorners[j], NearDepth, FarDepth, CascadeNear);
				CascadeCorners[j + 4] = InterpolateFrustumCorner(NearCorners[j], FarCorners[j], NearDepth, FarDepth, CascadeFar);
			}

			FVector Center = FVector::ZeroVector;
			for (const FVector& Corner : CascadeCorners)
			{
				Center += Corner;
			}
			Center *= 1.0f / 8.0f;

			float Radius = 1.0f;
			for (const FVector& Corner : CascadeCorners)
			{
				Radius = std::max(Radius, FVector::Dist(Center, Corner));
			}
			
			if (Light->IsShadowTexelSnapped())
			{
				const float TexelSize = (Radius * 2.0f) / static_cast<float>(FShadowAtlasManager::DirectionalCascadeResolution);
				const FVector LightForward = LightDirection.GetSafeNormal();
				const FVector LightRight = FVector::CrossProduct(MakeStableUpVector(LightForward), LightForward).GetSafeNormal();
				const FVector LightUp = FVector::CrossProduct(LightForward, LightRight).GetSafeNormal();

				const float CenterRight = FVector::DotProduct(Center, LightRight);
				const float CenterUp = FVector::DotProduct(Center, LightUp);
				const float SnappedRight = std::round(CenterRight / TexelSize) * TexelSize;
				const float SnappedUp = std::round(CenterUp / TexelSize) * TexelSize;

				Center += LightRight * (SnappedRight - CenterRight);
				Center += LightUp * (SnappedUp - CenterUp);
			}

			const FVector LightPosition = Center - LightDirection * Radius;

			const float ZNear = 0.0f;
			const float ZFar = Radius * 2.0f;

			const FMatrix LightView = FMatrix::MakeViewLookAtLH(LightPosition, Center, MakeStableUpVector(LightDirection));
			const FMatrix LightProjection = FMatrix::MakeOrthographicLH(Radius * 2.0f, Radius * 2.0f, ZNear, ZFar);
			OutResult.LightViewProj[i] = LightView * LightProjection;
			OutResult.CascadeRadius.XYZW[i] = Radius;
		}

		return true;
	}

	bool BuildDirectionalPSMViewProjection(
		const UDirectionalLightComponent* Light,
		const FRenderBus& RenderBus,
		const FVector& ToLight,
		FDirectionalPSMBuildResult& OutResult)
	{
		const FVector ToLightDirection = ToLight.GetSafeNormal();
		if (ToLightDirection.IsNearlyZero())
		{
			return false;
		}

		// Direct3D NDC: x/y=[-1, 1], z=[0, 1].
		const FVector CubeCenterPP(0.0f, 0.0f, 0.5f);
		constexpr float CubeRadiusPP = 1.5f;
		constexpr float WThreshold = 0.001f;
		constexpr float MinNearPlane = 0.1f;
		constexpr float MinPlaneGap = 0.001f;
		constexpr float MinFovPP = MathUtil::DegreesToRadians(1.0f);
		constexpr float MaxFovPP = MathUtil::DegreesToRadians(175.0f);

		FMatrix PSMCameraView = FMatrix::Identity;
		FMatrix PSMCameraProj = FMatrix::Identity;
		BuildPSMCameraViewProjection(Light, RenderBus, PSMCameraView, PSMCameraProj);

		const FVector EyeLightDirection = PSMCameraView.TransformVector(ToLightDirection);
		const FVector4 LightPP = TransformVector4ByMatrix(FVector4(EyeLightDirection, 0.0f), PSMCameraProj);
		const bool bUseOrthoMatrix = std::fabs(LightPP.W) <= WThreshold;
		const bool bLightIsBehindEye = LightPP.W < -WThreshold;

		FMatrix ViewPP = FMatrix::Identity;
		FMatrix ProjPP = FMatrix::Identity;

		if (bUseOrthoMatrix)
		{
			if (!BuildOrthographicPostProjectiveViewProjection(
				FVector(LightPP.X, LightPP.Y, LightPP.Z),
				CubeCenterPP,
				CubeRadiusPP,
				MinPlaneGap,
				ViewPP,
				ProjPP))
			{
				return false;
			}
		}
		else
		{
			const float InvW = 1.0f / LightPP.W;
			const FVector LightPositionPP(LightPP.X * InvW, LightPP.Y * InvW, LightPP.Z * InvW);

			const FVector LookAtCubePP = CubeCenterPP - LightPositionPP;
			const float DistToCube = LookAtCubePP.Size();
			if (DistToCube <= MathUtil::KindaSmallNumber)
			{
				return false;
			}

			// The original OpenGL PSM uses a negative-near projection when the
			// light is behind the eye. In our D3D LESS-depth path that reverses
			// depth ordering, so keep this case on an orthographic PP fallback.
			if (bLightIsBehindEye || DistToCube <= CubeRadiusPP + MinNearPlane)
			{
				FVector FallbackLightDirectionPP = bLightIsBehindEye
					? FVector(LightPP.X, LightPP.Y, LightPP.Z)
					: LightPositionPP - CubeCenterPP;
				if (FallbackLightDirectionPP.IsNearlyZero())
				{
					FallbackLightDirectionPP = FVector(LightPP.X, LightPP.Y, LightPP.Z);
				}

				if (!BuildOrthographicPostProjectiveViewProjection(
					FallbackLightDirectionPP,
					CubeCenterPP,
					CubeRadiusPP,
					MinPlaneGap,
					ViewPP,
					ProjPP))
				{
					return false;
				}
			}
			else
			{
				const FVector ViewDirectionPP = LookAtCubePP * (1.0f / DistToCube);
				const float SinHalfFovPP = MathUtil::Clamp(CubeRadiusPP / DistToCube, 0.0f, 1.0f);
				// asin gives the tangent cone that contains the whole bounding sphere.
				const float FovPP = MathUtil::Clamp(2.0f * std::asin(SinHalfFovPP), MinFovPP, MaxFovPP);
				const float NearPP = std::max(MinNearPlane, DistToCube - CubeRadiusPP);
				const float FarPP = std::max(NearPP + MinPlaneGap, DistToCube + CubeRadiusPP);

				ViewPP = FMatrix::MakeViewLookAtLH(LightPositionPP, CubeCenterPP, MakeStableUpVector(ViewDirectionPP));
				ProjPP = FMatrix::MakePerspectiveFovLH(FovPP, 1.0f, NearPP, FarPP);
			}
		}

		OutResult.LightViewProj = PSMCameraView * PSMCameraProj * ViewPP * ProjPP;
		return true;
	}

	void PackDirectionalCSMShadowConstants(const FDirectionalCSMBuildResult& BuildResult, FDirectionalShadowConstants& OutConstants)
	{
		for (int32 i = 0; i < MAX_CASCADE_COUNT; ++i)
		{
			OutConstants.LightViewProj[i] = BuildResult.LightViewProj[i];
		}

		OutConstants.SplitDistances = BuildResult.SplitDistances;
		OutConstants.CascadeRadius = BuildResult.CascadeRadius;
		OutConstants.ShadowMode = DirectionalShadowModeValue::CSM;
	}

	void PackDirectionalPSMShadowConstants(const FDirectionalPSMBuildResult& BuildResult, FDirectionalShadowConstants& OutConstants)
	{
		for (int32 i = 0; i < MAX_CASCADE_COUNT; ++i)
		{
			OutConstants.LightViewProj[i] = BuildResult.LightViewProj;
		}

		constexpr float PSMCascadeSplitSentinel = 1.0e30f;
		OutConstants.SplitDistances = FVector4(
			PSMCascadeSplitSentinel,
			PSMCascadeSplitSentinel,
			PSMCascadeSplitSentinel,
			PSMCascadeSplitSentinel);
		OutConstants.CascadeRadius = FVector4::ZeroVector();
		OutConstants.ShadowMode = DirectionalShadowModeValue::PSM;
	}

	FColor MakeBVHInternalNodeColor(int32 PathIndexFromLeaf, int32 PathLength)
	{
		if (PathLength <= 1)
		{
			return FColor::Yellow();
		}

		const float T = static_cast<float>(PathIndexFromLeaf) / static_cast<float>(PathLength - 1);
		return FColor::Lerp(FColor::Cyan(), FColor::Yellow(), T);
	}

	bool UsesCameraDependentRenderBounds(const UPrimitiveComponent* PrimitiveComponent)
	{
		if (PrimitiveComponent == nullptr)
		{
			return false;
		}

		switch (PrimitiveComponent->GetPrimitiveType())
		{
		case EPrimitiveType::EPT_Billboard:
		case EPrimitiveType::EPT_Text:
		case EPrimitiveType::EPT_SubUV:
			return true;
		default:
			return false;
		}
	}

	FMatrix MakeViewBillboardMatrix(const UPrimitiveComponent* Primitive, const FRenderBus& RenderBus)
	{
		const FMatrix WorldMatrix = Primitive->GetWorldMatrix();
		return UBillboardComponent::MakeBillboardWorldMatrix(
			WorldMatrix.GetOrigin(),
			WorldMatrix.GetScaleVector(),
			RenderBus.GetCameraForward(),
			RenderBus.GetCameraRight(),
			RenderBus.GetCameraUp());
	}

	FMatrix MakeViewSubUVSelectionMatrix(const USubUVComponent* SubUVComp, const FRenderBus& RenderBus)
	{
		const FVector WorldScale = SubUVComp->GetWorldScale();
		return USubUVComponent::MakeBillboardWorldMatrix(
			SubUVComp->GetWorldLocation(),
			FVector(
				WorldScale.X > 0.01f ? WorldScale.X : 0.01f,
				SubUVComp->GetWidth() * WorldScale.Y,
				SubUVComp->GetHeight() * WorldScale.Z),
			RenderBus.GetCameraForward(),
			RenderBus.GetCameraRight(),
			RenderBus.GetCameraUp());
	}
	/*
	* BillBoardComponent를 상속받은 text, SubUV가 사용하는 AABB 계산함수(의존성 분리)
	*/
	FAABB BuildQuadAABB(const FMatrix& WorldMatrix)
	{
		static constexpr FVector LocalQuadCorners[4] =
		{
			FVector(0.0f, -0.5f,  0.5f),
			FVector(0.0f,  0.5f,  0.5f),
			FVector(0.0f,  0.5f, -0.5f),
			FVector(0.0f, -0.5f, -0.5f)
		};

		FAABB Box;
		Box.Reset();

		for (const FVector& Corner : LocalQuadCorners)
		{
			Box.Expand(WorldMatrix.TransformPosition(Corner));
		}

		return Box;
	}

	FAABB BuildRenderAABB(const UPrimitiveComponent* PrimitiveComponent, const FRenderBus& RenderBus)
	{
		switch (PrimitiveComponent->GetPrimitiveType())
		{
		case EPrimitiveType::EPT_Billboard:
		{
			const UBillboardComponent* BillboardComponent = static_cast<const UBillboardComponent*>(PrimitiveComponent);
			return BuildQuadAABB(UBillboardComponent::MakeBillboardWorldMatrix(
				BillboardComponent->GetWorldLocation(),
				FVector(0.00f, BillboardComponent->GetWidth(), BillboardComponent->GetHeight()),
				RenderBus.GetCameraForward(),
				RenderBus.GetCameraRight(),
				RenderBus.GetCameraUp()));
		}
		case EPrimitiveType::EPT_Text:
		{
			const UTextRenderComponent* TextComp = static_cast<const UTextRenderComponent*>(PrimitiveComponent);
			return BuildQuadAABB(TextComp->GetTextMatrix());
		}
		case EPrimitiveType::EPT_SubUV:
		{
			const USubUVComponent* SubUVComp = static_cast<const USubUVComponent*>(PrimitiveComponent);
			return BuildQuadAABB(MakeViewSubUVSelectionMatrix(SubUVComp, RenderBus));
		}

		default:
			return PrimitiveComponent->GetWorldAABB();
		}
	}

	int32 SelectLODLevel(const FVector& CameraPos, const FAABB& Bounds, const FMatrix& ProjMatrix, int32 ValidLODCount)
	{
		bool IsOrthoGraphic = (std::abs(ProjMatrix.M[3][3] - 1.0f) < 1e-4f);
		if (ValidLODCount <= 1 || IsOrthoGraphic) return 0;

		// 1. 바운딩 박스를 통해 바운딩 스피어 반지름 및 카메라와의 거리 계산
		const FVector Center = (Bounds.Min + Bounds.Max) * 0.5f;
		const FVector Extent = (Bounds.Max - Bounds.Min) * 0.5f;
		const float SphereRadius = std::sqrt(Extent.X * Extent.X + Extent.Y * Extent.Y + Extent.Z * Extent.Z);

		const FVector Diff = Center - CameraPos;
		const float Dist = std::sqrt(Diff.X * Diff.X + Diff.Y * Diff.Y + Diff.Z * Diff.Z);

		if (Dist <= 1e-4f) return 0;

		const float ProjectedRadius = (SphereRadius / Dist) * ProjMatrix.M[2][1];
		const float ScreenCoverage = ProjectedRadius; 

		static constexpr float Thresholds[] = { 0.15f, 0.08f, 0.05f, 0.02f };
		static constexpr int32 ThresholdCount = static_cast<int32>(sizeof(Thresholds) / sizeof(Thresholds[0]));

		const int32 MaxLOD = ValidLODCount - 1;
		for (int32 LOD = 0; LOD < MaxLOD; ++LOD)
		{
			float Threshold = (LOD < ThresholdCount) ? Thresholds[LOD] : 0.0f;
			if (ScreenCoverage >= Threshold)
				return LOD;
		}

		// 화면에 차지하는 비율이 가장 낮을 경우 최하위 LOD 반환
		return MaxLOD;
	}
}
