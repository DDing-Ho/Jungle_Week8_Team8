#include "Editor/UI/EditorPropertyWidget.h"

#include "Editor/EditorEngine.h"
#include "ImGui/imgui.h"
#include "GameFramework/PrimitiveActors.h"
#include "Core/PropertyTypes.h"
#include "Math/Color.h"
#include "Core/ResourceManager.h"
#include "Core/Paths.h"
#include "Object/FName.h"
#include <cctype>
#include <cstring>
#include <functional>
#include <Windows.h>
#include <commdlg.h>
#include <filesystem>

#include "Editor/Utility/EditorComponentFactory.h"

#include "GameFramework/AActor.h"
#include "Component/LuaScriptComponent.h"
#include "Component/StaticMeshComponent.h"
#include "Component/GizmoComponent.h"
#include "Component/Light/LightComponent.h"
#include "Component/Movement/InterpToMovementComponent.h"
#include "Editor/Viewport/ViewportLayout.h"
#include "Editor/Utility/EditorUIUtils.h"
#include "Engine/Render/Renderer/RenderFlow/ShadowAtlasManager.h"
#include "Engine/Scripting/ScriptUtils.h"
#include "Editor/UI/EditorConsoleWidget.h"

#define SEPARATOR(); ImGui::Spacing(); ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing(); ImGui::Spacing();

namespace
{
	constexpr float ShadowPreviewSize = 256.0f;

	const ImU32 ColorGridLine = IM_COL32(255, 255, 255, 35);
	const ImU32 ColorEmptyBg = IM_COL32(0, 0, 0, 150);
	const ImU32 ColorEmptyBorder = IM_COL32(255, 255, 255, 100);
	const ImU32 ColorHighlightRect = IM_COL32(255, 220, 0, 220);
	const ImU32 ColorHighlightText = IM_COL32(255, 220, 0, 255);

	// ─────────────────── Light & Shadow ───────────────────
	void DrawAtlasGrid(ImDrawList* DrawList, const ImVec2& Min, const ImVec2& Max, uint32 GridDimension);
	void DrawEmptyShadowPreview(ImDrawList* DrawList, const ImVec2& Min, const ImVec2& Max);
	void DrawDirectionalShadowPreview(FRenderTargetSet* RenderTargets, ImDrawList* DrawList);
	void DrawSpotShadowPreview(ULightComponent* LightComp, FRenderTargetSet* RenderTargets, ImDrawList* DrawList);
	void OverrideCameraWithLightPerspective(ULightComponent* LightComp, UEditorEngine* EditorEngine);
	void VisualizeShadowMap(ULightComponent* LightComp, UEditorEngine* EditorEngine);

	// ─────────────────── Helper ───────────────────────────
	int32 ExtractActorID(const AActor* Actor);
	FString GetEditorScriptSceneName(UEditorEngine* EditorEngine);
	FString GetEditorScriptActorName(const AActor* Actor);
	FString MakeEditorLuaScriptPath(ULuaScriptComponent* Component, UEditorEngine* EditorEngine);
	FString GetLuaScriptDisplayPath(ULuaScriptComponent* Component);
	void RenderLuaScriptComponentActions(ULuaScriptComponent* Component, UEditorEngine* EditorEngine);
	bool OpenLuaScriptFileDialog(FString& OutFilePath);
}

void FEditorPropertyWidget::Initialize(UEditorEngine* InEditorEngine)
{
	FEditorWidget::Initialize(InEditorEngine);
	SelectionManager = &EditorEngine->GetSelectionManager();
}

// 현재 선택된 컴포넌트와 액터 정보를 초기화합니다.
void FEditorPropertyWidget::ResetSelection()
{
	SelectedComponent = nullptr;
	LastSelectedActor = nullptr;
	bActorSelected = true;
}

// 전체 프로퍼티 윈도우의 레이아웃을 구성하고 그리는 메인 함수입니다.
void FEditorPropertyWidget::Render(float DeltaTime)
{
	(void)DeltaTime;

	ImGui::SetNextWindowSize(ImVec2(350.0f, 500.0f), ImGuiCond_Once);
	ImGui::Begin("Jungle Property Window");

	AActor* PrimaryActor = SelectionManager->GetPrimarySelection();
	if (!PrimaryActor)
	{
		SelectedComponent = nullptr;
		LastSelectedActor = nullptr;
		bActorSelected = true;
		ImGui::Text("No object selected.");
		ImGui::End();
		return;
	}

	UpdateSelectionState(PrimaryActor);

	// SelectedComponent 유효성 검사 (다른 곳에서 삭제되었을 가능성 대비)
	if (SelectedComponent && !UObject::IsValid(SelectedComponent))
	{
		SelectedComponent = nullptr;
		bActorSelected = true;
	}

	const TArray<AActor*>& SelectedActors = SelectionManager->GetSelectedActors();

	// 상단 액터 정보 및 컨트롤 영역
	RenderActorHeaderRegion(PrimaryActor, SelectedActors);

	if (SelectionManager->GetPrimarySelection() == nullptr)
	{
		ImGui::End();
		return;
	}

	// 컴포넌트 트리 영역
	SEPARATOR();
	RenderComponentTree(PrimaryActor);

	// 디테일 프로퍼티 영역
	SEPARATOR();
	ImGui::Text("Details");
	ImGui::Separator();

	float ScrollHeight = std::max(UIConstants::MinScrollHeight, ImGui::GetContentRegionAvail().y);
	ImGui::BeginChild("##Details", ImVec2(0, ScrollHeight), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
	{
		RenderDetails(PrimaryActor, SelectedActors);
	}
	ImGui::EndChild();

	ImGui::End();
}

// 선택된 대상이 바뀔 때 내부 상태(컴포넌트 선택 여부 등)를 자동으로 갱신합니다.
void FEditorPropertyWidget::UpdateSelectionState(AActor* PrimaryActor)
{
	if (PrimaryActor != LastSelectedActor)
	{
		SelectedComponent = nullptr;
		LastSelectedActor = PrimaryActor;

		USceneComponent* RootComp = PrimaryActor->GetRootComponent();
		if (RootComp && RootComp->IsA<UStaticMeshComponent>())
		{
			SelectedComponent = RootComp;
			bActorSelected = false;
		}
		else if (RootComp && RootComp->IsA<ULightComponent>())
		{
			SelectedComponent = RootComp;
			bActorSelected = false;
		}
		else
		{
			bActorSelected = true;
		}
	}
}

// 단일 선택과 다중 선택 상황에 맞춰 상단 헤더 영역을 그립니다.
void FEditorPropertyWidget::RenderActorHeaderRegion(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors)
{
	const int32 SelectionCount = static_cast<int32>(SelectedActors.size());

	if (SelectionCount > 1)
	{
		RenderMultiSelectionHeader(PrimaryActor, SelectedActors, SelectionCount);
	}
	else
	{
		RenderSingleSelectionHeader(PrimaryActor);
	}
}

// 여러 액터가 선택되었을 때의 정보 표시와 일괄 삭제 기능을 제공합니다.
void FEditorPropertyWidget::RenderMultiSelectionHeader(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors, int32 SelectionCount)
{
	ImGui::Text("Class: %s", PrimaryActor->GetTypeInfo()->name);

	FString PrimaryName = PrimaryActor->GetFName().ToString();
	if (PrimaryName.empty())
		PrimaryName = PrimaryActor->GetTypeInfo()->name;

	if (bActorSelected)
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));

	ImGui::Text("Name: %s (+%d)", PrimaryName.c_str(), SelectionCount - 1);

	if (bActorSelected)
		ImGui::PopStyleColor();

	if (ImGui::IsItemClicked())
	{
		bActorSelected = true;
		SelectedComponent = nullptr;
	}

	ImGui::SameLine();
	char RemoveLabel[64];
	snprintf(RemoveLabel, sizeof(RemoveLabel), "Remove %d Objects", SelectionCount);
	if (ImGui::SmallButton(RemoveLabel))
	{
		for (AActor* Actor : SelectedActors)
		{
			if (Actor && Actor->GetFocusedWorld())
				Actor->GetFocusedWorld()->DestroyActor(Actor);
		}
		SelectionManager->ClearSelection();
		SelectedComponent = nullptr;
		LastSelectedActor = nullptr;
	}
}

// 단일 액터의 이름 표시와 새로운 컴포넌트를 추가할 수 있는 버튼을 렌더링합니다.
void FEditorPropertyWidget::RenderSingleSelectionHeader(AActor* PrimaryActor)
{
	// SelectedComponent가 아직 설정되지 않은 경우 루트로 초기화
	if (SelectedComponent == nullptr)
		SelectedComponent = PrimaryActor->GetRootComponent();

	if (bActorSelected) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.2f, 1.0f));
	if (bActorSelected) ImGui::PopStyleColor();

	FString ActorLabel = "Actor: " + PrimaryActor->GetFName().ToString();
	if (ImGui::Selectable(ActorLabel.c_str(), bActorSelected))
	{
		bActorSelected = true;
		SelectedComponent = nullptr;
	}
	
	ImGui::Text("Component: %s", SelectedComponent ? SelectedComponent->GetTypeInfo()->name : "None");

	ImGui::SameLine();
	if (ImGui::SmallButton("Remove"))
	{
		if (PrimaryActor->GetFocusedWorld())
			PrimaryActor->GetFocusedWorld()->DestroyActor(PrimaryActor);
		SelectionManager->ClearSelection();
		SelectedComponent = nullptr;
		LastSelectedActor = nullptr;
	}

	ImGui::Spacing();
	RenderAddComponentPopup(PrimaryActor);
}

// 클릭 시 액터에 추가 가능한 컴포넌트 목록을 팝업 형태로 보여줍니다.
void FEditorPropertyWidget::RenderAddComponentPopup(AActor* PrimaryActor)
{
	if (ImGui::Button("Add Component", ImVec2(-1, 0)))
	{
		ImGui::OpenPopup("AddComponentPopup");
	}

	if (ImGui::BeginPopup("AddComponentPopup"))
	{
		const char* CurrentCategory = nullptr;
		for (const FComponentMenuEntry& Entry : FEditorComponentFactory::GetMenuRegistry())
		{
			if (CurrentCategory == nullptr || strcmp(CurrentCategory, Entry.Category) != 0)
			{
				CurrentCategory = Entry.Category;
				ImGui::SeparatorText(CurrentCategory);
			}

			if (ImGui::Selectable(Entry.DisplayName))
			{
				if (UActorComponent* NewComp = Entry.Register(PrimaryActor))
				{
					AttachAndSelectNewComponent(PrimaryActor, NewComp);
				}
			}
		}
		ImGui::EndPopup();
	}
}

// 액터가 가진 모든 컴포넌트의 계층 구조와 목록을 렌더링합니다.
void FEditorPropertyWidget::RenderComponentTree(AActor* Actor)
{
	ImGui::Text("Components");
	ImGui::Separator();

	float TreeHeight = std::max(100.0f, ImGui::GetContentRegionAvail().y * 0.4f);

	// BeginChild를 호출하여 내부 스크롤이 가능한 Child Window를 생성합니다.
	ImGui::BeginChild("##ComponentTreeChild", ImVec2(0, TreeHeight), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);

	USceneComponent* Root = Actor->GetRootComponent();
	UActorComponent* ComponentToDelete = nullptr;

	if (Root)
	{
		RenderSceneComponentNode(Actor, Root, ComponentToDelete);
	}

	// Non-scene ActorComponents 및 MovementComponent들 하단 출력
	for (UActorComponent* Comp : Actor->GetComponents())
	{
		if (!Comp || Comp->IsA<USceneComponent>() || Comp->IsHiddenInEditor())
		{
			continue;
		}

		ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		if (!bActorSelected && SelectedComponent == Comp)
			Flags |= ImGuiTreeNodeFlags_Selected;

		float ClipMaxX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - UIConstants::ClipMargin;
		ImGui::PushClipRect(ImGui::GetWindowPos(), ImVec2(ClipMaxX, ImGui::GetWindowPos().y + 99999.f), true);

		if (UMovementComponent* MoveComp = Cast<UMovementComponent>(Comp))
		{
			FString MoveName = EditorUIUtils::GetMovementComponentDisplayName(MoveComp);
			ImGui::TreeNodeEx(Comp, Flags, "%s", MoveName.c_str());

			if (ImGui::BeginDragDropSource())
			{
				ImGui::SetDragDropPayload("DND_MOVE_COMP", &Comp, sizeof(UActorComponent*));
				ImGui::Text("Moving %s", MoveName.c_str());
				ImGui::EndDragDropSource();
			}
		}
		else
		{
			FString Name = Comp->GetFName().ToString();
			ImGui::TreeNodeEx(Comp, Flags, "%s", Name.c_str());
		}

		ImGui::PopClipRect();

		if (ImGui::IsItemClicked())
		{
			SelectedComponent = Comp;
			bActorSelected = false;
		}

		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - UIConstants::TreeRightMargin);
		char XId[64];
		EditorUIUtils::MakeXButtonId(XId, sizeof(XId), Comp);
		if (EditorUIUtils::DrawXButton(XId))
			ComponentToDelete = Comp;
	}

	ImGui::EndChild();

	// 삭제 처리는 렌더링 루프 바깥(Child Window 종료 후)에서 안전하게 수행
	if (ComponentToDelete)
	{
		// SelectedComponent가 삭제 대상이거나 그 자손이면 선택 해제
		auto IsAncestorOf = [](USceneComponent* Ancestor, UActorComponent* MaybeDescendant) -> bool
		{
			auto* SceneDesc = Cast<USceneComponent>(MaybeDescendant);
			for (USceneComponent* P = SceneDesc ? SceneDesc->GetParent() : nullptr; P; P = P->GetParent())
				if (P == Ancestor)
					return true;
			return false;
		};

		if (SelectedComponent == ComponentToDelete ||
			IsAncestorOf(Cast<USceneComponent>(ComponentToDelete), SelectedComponent))
		{
			SelectedComponent = nullptr;
			bActorSelected = true;
		}

		if (auto* SceneComp = Cast<USceneComponent>(ComponentToDelete))
			Actor->RemoveComponentWithChildren(SceneComp);
		else
			Actor->RemoveComponent(ComponentToDelete);
	}
}

// 씬 컴포넌트의 계층 구조를 재귀적으로 그리며 드래그 앤 드롭 이동을 지원합니다.
void FEditorPropertyWidget::RenderSceneComponentNode(AActor* Actor, USceneComponent* Comp, UActorComponent*& OutCompToDelete)
{
	if (!Comp || Comp->IsHiddenInEditor())
		return;

	// 노드 이름 설정
	FString Name = Comp->GetFName().ToString();
	if (Name.empty())
		Name = Comp->GetTypeInfo()->name;

	// 숨김 처리된 자식은 제외하고 표시할 자식이 있는지 확인
	bool bHasVisibleChildren = false;
	for (USceneComponent* Child : Comp->GetChildren())
		if (!Child->IsHiddenInEditor())
		{
			bHasVisibleChildren = true;
			break;
		}

	// 노드 이름, 자식 존재 여부에 따라 Tree Flag 설정
	ImGuiTreeNodeFlags Flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
	if (!bHasVisibleChildren)
		Flags |= ImGuiTreeNodeFlags_Leaf;
	if (!bActorSelected && SelectedComponent == Comp)
		Flags |= ImGuiTreeNodeFlags_Selected;

	// 트리 노드 출력
	bool bIsRoot = (Comp->GetParent() == nullptr);
	if (!bIsRoot)
	{
		float ClipMaxX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - UIConstants::ClipMargin;
		ImGui::PushClipRect(ImGui::GetWindowPos(), ImVec2(ClipMaxX, ImGui::GetWindowPos().y + 99999.f), true);
	}

	bool bOpen = ImGui::TreeNodeEx(Comp, Flags, "%s%s", bIsRoot ? "[Root] " : "", Name.c_str());

	if (!bIsRoot)
	{
		ImGui::PopClipRect();
	}

	// 드래그 앤 드롭 (계층 구조 변경 및 MovementComponent가 이동시킬 대상 설정)
	if (ImGui::BeginDragDropSource())
	{
		ImGui::SetDragDropPayload("DND_SCENE_COMP", &Comp, sizeof(USceneComponent*));
		ImGui::Text("Dragging %s", Name.c_str());
		ImGui::EndDragDropSource();
	}

	if (ImGui::BeginDragDropTarget())
	{
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("DND_SCENE_COMP"))
		{
			USceneComponent* DraggedComp = *static_cast<USceneComponent**>(Payload->Data);

			// 조상 여부 체크 (순환 참조 방지)
			bool bIsAncestor = false;
			for (USceneComponent* P = Comp; P; P = P->GetParent())
				if (P == DraggedComp)
				{
					bIsAncestor = true;
					break;
				}

			if (DraggedComp && DraggedComp != Comp && !bIsAncestor)
				DraggedComp->AttachToComponent(Comp);
		}
		if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload("DND_MOVE_COMP"))
		{
			if (auto* DraggedMoveComp = *static_cast<UMovementComponent**>(Payload->Data))
				DraggedMoveComp->SetUpdatedComponent(Comp);
		}
		ImGui::EndDragDropTarget();
	}

	// 컴포넌트가 선택될 경우 자식 노드 재귀 호출
	if (ImGui::IsItemClicked())
	{
		SelectedComponent = Comp;
		bActorSelected = false;
	}

	if (bOpen && Comp)
	{
		for (USceneComponent* Child : Comp->GetChildren())
			RenderSceneComponentNode(Actor, Child, OutCompToDelete);
		ImGui::TreePop();
	}

	// 루트를 제외한 모든 컴포넌트에 삭제 버튼 표시
	if (!bIsRoot)
	{
		ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - UIConstants::TreeRightMargin);
		char XId[64];
		EditorUIUtils::MakeXButtonId(XId, sizeof(XId), Comp);
		if (EditorUIUtils::DrawXButton(XId)) OutCompToDelete = Comp;
	}
}

// 현재 선택된 대상(액터 또는 컴포넌트)에 맞는 세부 속성 창을 분기하여 렌더링합니다.
void FEditorPropertyWidget::RenderDetails(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors)
{
	if (bActorSelected)
	{
		ImGui::PushID(PrimaryActor);
		RenderActorProperties(PrimaryActor, SelectedActors);
		ImGui::PopID();
	}
	else if (SelectedComponent)
	{
		ImGui::PushID(SelectedComponent);
		RenderComponentProperties();
		ImGui::PopID();
	}
	else
	{
		ImGui::TextDisabled("Select an actor or component to view details.");
	}
}

// 선택된 액터의 트랜스폼(위치, 회전, 크기) 및 가시성 속성을 편집하는 UI를 그립니다.
void FEditorPropertyWidget::RenderActorProperties(AActor* PrimaryActor, const TArray<AActor*>& SelectedActors)
{
	ImGui::Text("Actor: %s", PrimaryActor->GetTypeInfo()->name);
	RenderEditableName("Name##Actor", PrimaryActor); // 편집 가능한 UI

	if (PrimaryActor->GetRootComponent())
	{
		ImGui::Separator();
		ImGui::Text("Transform");
		ImGui::Spacing();

		// FVector(위치, 회전, 크기)를 읽어서 Properties를 그려 주는 단순한 친구입니다.
		auto DrawTransformField = [&](const char* Label, FVector CurrentValue, auto ApplyFunc)
		{
			float Arr[3] = { CurrentValue.X, CurrentValue.Y, CurrentValue.Z };
			if (ImGui::DragFloat3(Label, Arr, 0.1f))
			{
				FVector Delta = FVector(Arr[0], Arr[1], Arr[2]) - CurrentValue;
				for (AActor* Actor : SelectedActors)
				{
					if (Actor) ApplyFunc(Actor, Delta);
				}
				EditorEngine->GetGizmo()->UpdateGizmoTransform();
			}
		};

		// Location, Rotation, Scale을 한 번에 그려줍니다.
		DrawTransformField("Location", PrimaryActor->GetActorLocation(), [](AActor* A, FVector D) { A->AddActorWorldOffset(D); });
		DrawTransformField("Rotation", PrimaryActor->GetActorRotation(), [](AActor* A, FVector D) { A->SetActorRotation(A->GetActorRotation() + D); });
		DrawTransformField("Scale",    PrimaryActor->GetActorScale(),    [](AActor* A, FVector D) { A->SetActorScale(A->GetActorScale() + D); });
	}

	ImGui::Separator();
	bool bVisible = PrimaryActor->IsVisible();
	if (ImGui::Checkbox("Visible", &bVisible))
	{
		PrimaryActor->SetVisible(bVisible);
	}
}

// 컴포넌트의 모든 편집 가능 속성들을 가져와 자동으로 적절한 위젯들을 렌더링합니다.
void FEditorPropertyWidget::RenderComponentProperties()
{
	ImGui::Text("Component: %s", SelectedComponent->GetTypeInfo()->name);
	RenderEditableName("Name##Component", SelectedComponent); // 편집 가능한 UI

	ImGui::Separator();

	// PropertyDescriptor 기반 자동 위젯 렌더링
	TArray<FPropertyDescriptor> Props;
	SelectedComponent->GetEditableProperties(Props);

	AActor* Owner = SelectedComponent->GetOwner();

	bool bAnyChanged = false;
	const bool bIsLuaScriptComponent = SelectedComponent->IsA<ULuaScriptComponent>();
	for (auto& Prop : Props)
	{
		if (bIsLuaScriptComponent && strcmp(Prop.Name, "Script Path") == 0)
		{
			continue;
		}

		if (Prop.Type == EPropertyType::SceneComponentRef)
		{
			RenderSceneComponentRefWidget(Prop, Owner);
		}
		else
		{
			bAnyChanged |= RenderPropertyWidget(Prop);
		}
	}
	// Special: InterpToMovementComponent control points + behaviour + actions
	if (UInterpToMovementComponent* InterpComp = Cast<UInterpToMovementComponent>(SelectedComponent))
	{
		RenderInterpControlPoints(InterpComp);
	}

	if (ULuaScriptComponent* LuaComp = Cast<ULuaScriptComponent>(SelectedComponent))
	{
		RenderLuaScriptComponentActions(LuaComp, EditorEngine);
	}

	// Special: Light component — override camera with light's perspective
	if (SelectedComponent->IsA<ULightComponent>())
	{
		ULightComponent* LightComp = static_cast<ULightComponent*>(SelectedComponent);
		SEPARATOR();
		if (ImGui::Button("Override Camera with Light's Perspective", ImVec2(-1, 0)))
		{
			OverrideCameraWithLightPerspective(LightComp, EditorEngine);
		}

		VisualizeShadowMap(LightComp, EditorEngine);
	}

	ImGui::Separator();

	// 변경이 있을 경우에만 월드 행렬 갱신
	if (bAnyChanged && SelectedComponent->IsA<USceneComponent>())
	{
		static_cast<USceneComponent*>(SelectedComponent)->MarkTransformDirty();
		SelectionManager->GetGizmo()->UpdateGizmoTransform();
	}
}

// 다른 씬 컴포넌트를 참조할 수 있도록 액터 내 컴포넌트 목록을 드롭다운으로 보여줍니다.
void FEditorPropertyWidget::RenderSceneComponentRefWidget(FPropertyDescriptor& Prop, AActor* Owner)
{
	// ValuePtr은 USceneComponent* 변수의 주소 (USceneComponent**)
	USceneComponent** ValuePtr = reinterpret_cast<USceneComponent**>(Prop.ValuePtr);
	USceneComponent* CurrentComp = *ValuePtr;

	// 액터 소유 SceneComponent 목록 수집
	TArray<USceneComponent*> SceneComps;
	SceneComps.push_back(nullptr); // "None" 선택지
	if (Owner)
	{
		for (UActorComponent* Comp : Owner->GetComponents())
		{
			if (USceneComponent* SceneComp = Cast<USceneComponent>(Comp))
				SceneComps.push_back(SceneComp);
		}
	}

	// 드롭다운 레이블 생성: "[Root] ClassName" 또는 "ClassName [FName]"
	auto GetLabel = [&](USceneComponent* Comp) -> FString
	{
		if (!Comp)
			return "None";
		FString Name = Comp->GetFName().ToString();
		if (Name.empty())
			Name = Comp->GetTypeInfo()->name;
		bool bIsRoot = Owner && (Comp == Owner->GetRootComponent());
		return bIsRoot ? ("[Root] " + Name) : Name;
	};

	FString CurrentLabel = GetLabel(CurrentComp);
	if (ImGui::BeginCombo(Prop.Name, CurrentLabel.c_str()))
	{
		for (USceneComponent* SceneComp : SceneComps)
		{
			bool bSelected = (SceneComp == CurrentComp);
			// ##ptr 으로 포인터를 ID로 사용하여 동일 이름 컴포넌트를 구별
			char SelectableId[128];
			snprintf(SelectableId, sizeof(SelectableId), "%s##%p",
					 GetLabel(SceneComp).c_str(), static_cast<void*>(SceneComp));
			if (ImGui::Selectable(SelectableId, bSelected))
			{
				*ValuePtr = SceneComp;
				SelectedComponent->PostEditProperty(Prop.Name);
			}
			if (bSelected)
				ImGui::SetItemDefaultFocus();
		}
		ImGui::EndCombo();
	}
}

// 개별 데이터 타입(bool, float, Vec3 등)에 맞는 최적화된 ImGui 위젯을 렌더링합니다.
bool FEditorPropertyWidget::RenderPropertyWidget(FPropertyDescriptor& Prop)
{
	bool bChanged = false;

	switch (Prop.Type)
	{
	case EPropertyType::Bool:
	{
		bool* Val = static_cast<bool*>(Prop.ValuePtr);
		bChanged = ImGui::Checkbox(Prop.Name, Val);
		break;
	}
	case EPropertyType::Int:
	{
		int32* Val = static_cast<int32*>(Prop.ValuePtr);
		bChanged = ImGui::DragInt(Prop.Name, Val);
		break;
	}
	case EPropertyType::Float:
	{
		float* Val = static_cast<float*>(Prop.ValuePtr);
		if (Prop.Min != 0.0f || Prop.Max != 0.0f)
			bChanged = ImGui::DragFloat(Prop.Name, Val, Prop.Speed, Prop.Min, Prop.Max);
		else
			bChanged = ImGui::DragFloat(Prop.Name, Val, Prop.Speed);
		break;
	}
	case EPropertyType::Vec3:
	{
		float* Val = static_cast<float*>(Prop.ValuePtr);
		bChanged = ImGui::DragFloat3(Prop.Name, Val, Prop.Speed);
		break;
	}
	case EPropertyType::Vec4:
	{
		float* Val = static_cast<float*>(Prop.ValuePtr);
		bChanged = ImGui::ColorEdit4(Prop.Name, Val);
		break;
	}
	case EPropertyType::Color:
	{
		FColor* Val = static_cast<FColor*>(Prop.ValuePtr);
		bChanged = ImGui::ColorEdit4(Prop.Name, &Val->R);
		break;
	}
	case EPropertyType::String:
	{
		FString* Val = static_cast<FString*>(Prop.ValuePtr);
		TArray<FString> Options;
		if (strcmp(Prop.Name, "Texture Path") == 0)
			Options = FResourceManager::Get().GetTextureFilePath();
		else if (strcmp(Prop.Name, "StaticMesh") == 0)
			Options = FResourceManager::Get().GetStaticMeshPaths();
		bChanged = EditorUIUtils::RenderStringComboOrInput(Prop.Name, *Val, Options);
		break;
	}
	case EPropertyType::Name:
	{
		FName* Val = static_cast<FName*>(Prop.ValuePtr);
		FString Current = Val->ToString();
		TArray<FString> Options;
		if (strcmp(Prop.Name, "Font") == 0)
			Options = FResourceManager::Get().GetFontNames();
		else if (strcmp(Prop.Name, "Particle") == 0)
			Options = FResourceManager::Get().GetParticleNames();
		if (EditorUIUtils::RenderStringComboOrInput(Prop.Name, Current, Options))
		{
			*Val = FName(Current);
			bChanged = true;
		}
		break;
	}
	case EPropertyType::Enum:
	{
		int* Val = static_cast<int*>(Prop.ValuePtr);
		if (Prop.EnumNames && Prop.EnumCount)
			bChanged = ImGui::Combo(Prop.Name, Val, Prop.EnumNames, Prop.EnumCount);
		break;
	}
	case EPropertyType::Vec3Array:
	{
		TArray<FVector>* Arr = static_cast<TArray<FVector>*>(Prop.ValuePtr);
		int32 ToRemove = -1;

		ImGui::Text("%s", Prop.Name);
		ImGui::Spacing();

		for (int32 i = 0; i < static_cast<int32>(Arr->size()); i++)
		{
			ImGui::PushID(i);

			float Val[3] = { (*Arr)[i].X, (*Arr)[i].Y, (*Arr)[i].Z };
			char Label[32];
			snprintf(Label, sizeof(Label), "[%d]", i);

			ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - UIConstants::XButtonSize - 8.0f);
			if (ImGui::DragFloat3(Label, Val, 1.0f))
			{
				(*Arr)[i] = FVector(Val[0], Val[1], Val[2]);
				bChanged = true;
			}

			ImGui::SameLine();
			char XId[32];
			snprintf(XId, sizeof(XId), "##rm_%d", i);
			if (EditorUIUtils::DrawXButton(XId))
				ToRemove = i;

			ImGui::PopID();
		}

		if (ToRemove >= 0)
		{
			Arr->erase(Arr->begin() + ToRemove);
			bChanged = true;
		}

		char AddLabel[64];
		snprintf(AddLabel, sizeof(AddLabel), "+ Add##%s", Prop.Name);
		if (ImGui::Button(AddLabel, ImVec2(-1, 0)))
		{
			Arr->push_back(Arr->empty() ? FVector(0.f, 0.f, 0.f) : Arr->back());
			bChanged = true;
		}
		break;
	}
	}

	if (bChanged && SelectedComponent)
	{
		SelectedComponent->PostEditProperty(Prop.Name);
	}

	return bChanged;
}

// 보간 이동 컴포넌트(InterpToMovement)의 실행, 중지, 리셋 버튼을 렌더링합니다.
void FEditorPropertyWidget::RenderInterpControlPoints(UInterpToMovementComponent* Comp)
{
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Text("Playback");
	ImGui::Spacing();

	float HalfWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
	if (ImGui::Button("Initiate", ImVec2(HalfWidth, 0)))
		Comp->Initiate();
	ImGui::SameLine();
	if (ImGui::Button("Stop", ImVec2(HalfWidth, 0)))
		Comp->ResetAndHalt();
	if (ImGui::Button("Reset", ImVec2(-1, 0)))
		Comp->Reset();
}

// 새로 생성된 컴포넌트를 액터의 부모 컴포넌트에 부착하고 선택 상태로 갱신합니다.
void FEditorPropertyWidget::AttachAndSelectNewComponent(AActor* PrimaryActor, UActorComponent* NewComp)
{
	if (!PrimaryActor || !NewComp)
		return;

	USceneComponent* AttachTarget = nullptr;
	if (SelectedComponent && SelectedComponent->IsA<USceneComponent>())
	{
		AttachTarget = static_cast<USceneComponent*>(SelectedComponent);
	}
	else
	{
		AttachTarget = PrimaryActor->GetRootComponent();
	}

	if (USceneComponent* SceneComp = Cast<USceneComponent>(NewComp))
	{
		if (AttachTarget)
			SceneComp->AttachToComponent(AttachTarget);
		else
			PrimaryActor->SetRootComponent(SceneComp);
	}
	else if (UMovementComponent* MoveComp = Cast<UMovementComponent>(NewComp))
	{
		if (AttachTarget)
			MoveComp->SetUpdatedComponent(AttachTarget);
	}

	if (ULuaScriptComponent* LuaComp = Cast<ULuaScriptComponent>(NewComp))
	{
		LuaComp->SetScriptPath(FScriptUtils::MakeActorScriptPath(
			GetEditorScriptSceneName(EditorEngine),
			GetEditorScriptActorName(PrimaryActor)));
	}

	SelectedComponent = NewComp;
	bActorSelected = false;
}

// ─────────────────── namespace ───────────────────────────────────────────────

namespace
{
	void VisualizeShadowMap(ULightComponent* LightComp, UEditorEngine* EditorEngine)
	{
		if (LightComp == nullptr || EditorEngine == nullptr) return;

		FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
		const int32 ViewportIndex = Layout.GetLastFocusedViewportIndex();
		FSceneViewport& SceneViewport = Layout.GetSceneViewport(ViewportIndex);
		FRenderTargetSet* RenderTargets = SceneViewport.GetRenderTargetSet();

		ImGui::Spacing();
		ImGui::Text("Depth Preview");

		if (!LightComp->IsCastShadows())
		{
			ImGui::TextDisabled("Cast Shadows is disabled.");
			return;
		}

		if (RenderTargets == nullptr)
		{
			ImGui::TextDisabled("No viewport render target.");
			return;
		}

		ImDrawList* DrawList = ImGui::GetWindowDrawList();

		switch (LightComp->GetLightType())
		{
		case ELightType::LightType_Directional:
			DrawDirectionalShadowPreview(RenderTargets, DrawList);
			break;

		case ELightType::LightType_Spot:
			DrawSpotShadowPreview(LightComp, RenderTargets, DrawList);
			break;

		case ELightType::LightType_Point:
			// TODO: Point Light 구현
			ImGui::TextDisabled("Point shadow atlas is not implemented.");
			break;

		case ELightType::LightType_AmbientLight:
			ImGui::TextDisabled("Ambient lights do not use shadow maps.");
			break;
		}
	}

	void DrawAtlasGrid(ImDrawList* DrawList, const ImVec2& Min, const ImVec2& Max, uint32 GridDimension)
	{
		if (DrawList == nullptr || GridDimension <= 1) return;

		const float CellSizeX = (Max.x - Min.x) / static_cast<float>(GridDimension);
		const float CellSizeY = (Max.y - Min.y) / static_cast<float>(GridDimension);

		for (uint32 Line = 1; Line < GridDimension; ++Line)
		{
			const float X = Min.x + CellSizeX * static_cast<float>(Line);
			const float Y = Min.y + CellSizeY * static_cast<float>(Line);
			DrawList->AddLine(ImVec2(X, Min.y), ImVec2(X, Max.y), ColorGridLine);
			DrawList->AddLine(ImVec2(Min.x, Y), ImVec2(Max.x, Y), ColorGridLine);
		}
	}

	void DrawEmptyShadowPreview(ImDrawList* DrawList, const ImVec2& Min, const ImVec2& Max)
	{
		if (DrawList == nullptr) return;
		DrawList->AddRectFilled(Min, Max, ColorEmptyBg);
		DrawList->AddRect(Min, Max, ColorEmptyBorder);
	}

	void DrawDirectionalShadowPreview(FRenderTargetSet* RenderTargets, ImDrawList* DrawList)
	{
		const bool bHasShadowMap = RenderTargets != nullptr && RenderTargets->DirectionalShadowSRV != nullptr;
		if (bHasShadowMap)
		{
			ImGui::Image(reinterpret_cast<ImTextureID>(RenderTargets->DirectionalShadowSRV), ImVec2(ShadowPreviewSize, ShadowPreviewSize));
		}
		else
		{
			ImGui::Dummy(ImVec2(ShadowPreviewSize, ShadowPreviewSize));
		}

		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		if (!bHasShadowMap)
		{
			DrawEmptyShadowPreview(DrawList, Min, Max);
		}
		else if (DrawList != nullptr)
		{
			DrawList->AddRect(Min, Max, ColorEmptyBorder);
		}

		DrawAtlasGrid(DrawList, Min, Max, FShadowAtlasManager::DirectionalAtlasGridDimension);
		if (DrawList == nullptr)
		{
			return;
		}

		const TArray<FDirectionalAtlasSlotDesc>& CascadeSlots = FShadowAtlasManager::GetDirectionalCascadeSlots();
		for (const FDirectionalAtlasSlotDesc& Slot : CascadeSlots)
		{
			const float X0 = Min.x + (static_cast<float>(Slot.X) / FShadowAtlasManager::DirectionalAtlasResolution) * ShadowPreviewSize;
			const float Y0 = Min.y + (static_cast<float>(Slot.Y) / FShadowAtlasManager::DirectionalAtlasResolution) * ShadowPreviewSize;
			const float X1 = Min.x + (static_cast<float>(Slot.X + Slot.Width) / FShadowAtlasManager::DirectionalAtlasResolution) * ShadowPreviewSize;
			const float Y1 = Min.y + (static_cast<float>(Slot.Y + Slot.Height) / FShadowAtlasManager::DirectionalAtlasResolution) * ShadowPreviewSize;

			DrawList->AddRect(ImVec2(X0, Y0), ImVec2(X1, Y1), ColorHighlightRect, 0.0f, 0, 2.0f);

			char Label[16];
			snprintf(Label, sizeof(Label), "C%u", Slot.CascadeIndex);
			DrawList->AddText(ImVec2(X0 + 4.0f, Y0 + 4.0f), ColorHighlightText, Label);
		}
	}

	void DrawSpotShadowPreview(ULightComponent* LightComp, FRenderTargetSet* RenderTargets, ImDrawList* DrawList)
	{
		const bool bHasShadowMap = RenderTargets != nullptr && RenderTargets->SpotShadowSRV != nullptr && RenderTargets->SpotShadowCount > 0;
		const int32 SelectedLightId = ExtractActorID(LightComp ? LightComp->GetOwner() : nullptr);
		const FSpotAtlasSlotDesc* SelectedSlot = nullptr;
		const TArray<FSpotAtlasSlotDesc>& ActiveSlots = FShadowAtlasManager::GetActiveSpotSlots();
		for (const FSpotAtlasSlotDesc& Slot : ActiveSlots)
		{
			if (SelectedLightId >= 0 && Slot.DebugLightId == SelectedLightId)
			{
				SelectedSlot = &Slot;
				break;
			}
		}

		if (bHasShadowMap && SelectedSlot != nullptr)
		{
			const ImVec2 UV0(SelectedSlot->AtlasRect.X, SelectedSlot->AtlasRect.Y);
			const ImVec2 UV1(SelectedSlot->AtlasRect.X + SelectedSlot->AtlasRect.Z, SelectedSlot->AtlasRect.Y + SelectedSlot->AtlasRect.W);
			ImGui::Image(reinterpret_cast<ImTextureID>(RenderTargets->SpotShadowSRV), ImVec2(ShadowPreviewSize, ShadowPreviewSize), UV0, UV1);

			const ImVec2 Min = ImGui::GetItemRectMin();
			const ImVec2 Max = ImGui::GetItemRectMax();
			if (DrawList != nullptr)
			{
				DrawList->AddRect(Min, Max, ColorHighlightText, 0.0f, 0, 2.0f);
			}
			ImGui::TextDisabled("Atlas Slot: %u x %u", SelectedSlot->Width, SelectedSlot->Height);
			return;
		}

		ImGui::Dummy(ImVec2(ShadowPreviewSize, ShadowPreviewSize));
		const ImVec2 Min = ImGui::GetItemRectMin();
		const ImVec2 Max = ImGui::GetItemRectMax();
		DrawEmptyShadowPreview(DrawList, Min, Max);

		if (!bHasShadowMap)
		{
			ImGui::TextDisabled("No spot shadow atlas.");
		}
		else
		{
			ImGui::TextDisabled("This spot light has no allocated shadow slot.");
		}
	}

	void OverrideCameraWithLightPerspective(ULightComponent* LightComp, UEditorEngine* EditorEngine)
	{
		FEditorViewportLayout& Layout = EditorEngine->GetViewportLayout();
		FEditorViewportClient* Client = Layout.GetViewportClient(Layout.GetLastFocusedViewportIndex());
		if (Client == nullptr)
			return;

		FViewportCamera* Camera = Client->GetCamera();
		if (Camera == nullptr)
			return;

		Camera->SetLocation(LightComp->GetWorldLocation());
		Camera->SetRotation(LightComp->GetWorldTransform().GetRotation());
		Camera->ClearCustomLookDir();
		Camera->SetProjectionType(EViewportProjectionType::Perspective);
		Client->SyncCameraTarget();
	}

	// 선택된 light를 소유한 actor ID 추출
	int32 ExtractActorID(const AActor* Actor)
	{
		if (Actor == nullptr) return -1;

		const FString Name = Actor->GetFName().ToString();
		int32 Cursor = static_cast<int32>(Name.size()) - 1;
		while (Cursor >= 0 && std::isdigit(static_cast<unsigned char>(Name[Cursor])))
		{
			--Cursor;
		}

		if (Cursor == static_cast<int32>(Name.size()) - 1) return -1;

		int32 Result = 0;
		for (int32 Index = Cursor + 1; Index < static_cast<int32>(Name.size()); ++Index)
		{
			Result = Result * 10 + (Name[Index] - '0');
		}

		return Result;
	}

	FString GetEditorScriptSceneName(UEditorEngine* EditorEngine)
	{
		if (EditorEngine == nullptr)
		{
			return "DefaultScene";
		}

		const char* SceneName = EditorEngine->GetMainPanel().GetSceneWidget().GetCurrentSceneName();
		return (SceneName && SceneName[0] != '\0') ? FString(SceneName) : FString("DefaultScene");
	}

	FString GetEditorScriptActorName(const AActor* Actor)
	{
		if (Actor == nullptr)
		{
			return "";
		}

		FString ActorName = Actor->GetFName().ToString();
		if (ActorName.empty() && Actor->GetTypeInfo())
		{
			ActorName = Actor->GetTypeInfo()->name;
		}
		return ActorName;
	}

	FString MakeEditorLuaScriptPath(ULuaScriptComponent* Component, UEditorEngine* EditorEngine)
	{
		AActor* Owner = Component ? Component->GetOwner() : nullptr;
		return FScriptUtils::MakeActorScriptPath(GetEditorScriptSceneName(EditorEngine), GetEditorScriptActorName(Owner));
	}

	FString GetLuaScriptDisplayPath(ULuaScriptComponent* Component)
	{
		if (Component != nullptr && !Component->GetScriptPath().empty())
		{
			return Component->GetScriptPath();
		}

		return "";
	}

	void RenderLuaScriptComponentActions(ULuaScriptComponent* Component, UEditorEngine* EditorEngine)
	{
		if (Component == nullptr)
		{
			return;
		}

		if (Component->GetScriptPath().empty())
		{
			Component->SetScriptPath(MakeEditorLuaScriptPath(Component, EditorEngine));
		}

		FString ScriptPath = GetLuaScriptDisplayPath(Component);

		ImGui::Spacing();
		ImGui::Separator();
		ImGui::Text("Lua Script");
		ImGui::Spacing();

		const FString ScriptFileName = FPaths::ToUtf8(std::filesystem::path(FPaths::ToWide(ScriptPath)).filename().wstring());
        char ScriptFileNameBuffer[MAX_PATH] = {};
        strncpy_s(ScriptFileNameBuffer, sizeof(ScriptFileNameBuffer), ScriptFileName.c_str(), _TRUNCATE);

        ImGui::BeginDisabled();
        ImGui::SetNextItemWidth(-1);
        ImGui::InputText("##ScriptFileName", ScriptFileNameBuffer, sizeof(ScriptFileNameBuffer), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        {
            ImGui::SetTooltip("%s", ScriptPath.c_str());
        }

		FString Error;
		const bool bScriptExists = FScriptUtils::DoesFileExist(ScriptPath);
		if (!bScriptExists)
		{
			if (ImGui::Button("Create Script", ImVec2(-1, 0)))
			{
				if (!Component->EnsureScriptFile())
				{
					const FString& LastError = Component->GetLastScriptError();
					if (!LastError.empty())
					{
						UE_LOG("%s", LastError.c_str());
					}
				}
				else
				{
					ScriptPath = Component->GetScriptPath();
				}
			}
		}
		else
		{
			if (ImGui::Button("Edit Script", ImVec2(-1, 0)))
			{
				if (!FScriptUtils::OpenScript(ScriptPath, &Error))
				{
					UE_LOG("%s", Error.c_str());
				}
			}
		}

		if (ImGui::Button("Browse Script", ImVec2(-1, 0)))
		{
			FString PickedPath;
			if (OpenLuaScriptFileDialog(PickedPath))
			{
				const FString RelativePath = FPaths::ToRelativeString(FPaths::ToWide(PickedPath));
				Component->SetScriptPath(RelativePath);
				ScriptPath = RelativePath;
			}
		}

		if (ImGui::Button("Reload Script", ImVec2(-1, 0)))
		{
			if (!Component->ReloadScript())
			{
				const FString& LastError = Component->GetLastScriptError();
				if (!LastError.empty())
				{
					UE_LOG("%s", LastError.c_str());
				}
			}
		}
	}

	bool OpenLuaScriptFileDialog(FString& OutFilePath)
	{
		OutFilePath.clear();

		std::filesystem::path ScriptDir(FPaths::ToAbsolute(FPaths::ToWide(FScriptUtils::GetScriptDirectory())));
		ScriptDir = ScriptDir.lexically_normal();
		ScriptDir.make_preferred();

		std::error_code Ec;
		std::filesystem::create_directories(ScriptDir, Ec);

		WCHAR FileBuffer[MAX_PATH] = { 0 };
		const std::filesystem::path OpenPattern = ScriptDir / L"*.lua";
		wcsncpy_s(FileBuffer, MAX_PATH, OpenPattern.wstring().c_str(), _TRUNCATE);
		const std::wstring InitialDir = ScriptDir.wstring();

		const std::filesystem::path PrevCwd = std::filesystem::current_path();
		std::error_code ChdirEc;
		std::filesystem::current_path(ScriptDir, ChdirEc);

		OPENFILENAMEW DialogDesc = {};
		DialogDesc.lStructSize = sizeof(DialogDesc);
		DialogDesc.hwndOwner = static_cast<HWND>(ImGui::GetMainViewport()->PlatformHandleRaw);
		DialogDesc.lpstrFilter = L"Lua Scripts (*.lua)\0*.lua\0All Files (*.*)\0*.*\0";
		DialogDesc.lpstrFile = FileBuffer;
		DialogDesc.nMaxFile = MAX_PATH;
		DialogDesc.lpstrInitialDir = InitialDir.c_str();
		DialogDesc.lpstrDefExt = L"lua";
		DialogDesc.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

		const BOOL bPicked = GetOpenFileNameW(&DialogDesc);

		std::error_code RestoreEc;
		std::filesystem::current_path(PrevCwd, RestoreEc);

		if (!bPicked)
		{
			return false;
		}

		OutFilePath = FPaths::ToUtf8(FileBuffer);
		return true;
	}
}

// 액터나 컴포넌트의 이름을 입력 창을 통해 실시간으로 수정할 수 있게 합니다.
template <typename T>
void FEditorPropertyWidget::RenderEditableName(const char* Label, T* TargetObject)
{
	if (!TargetObject) return;

	char NameBuf[256];
	strncpy_s(NameBuf, sizeof(NameBuf), TargetObject->GetFName().ToString().c_str(), _TRUNCATE);

	// Enter 키를 누르거나 포커스를 잃었을 경우에 이름이 변경되도록 설정
	if (ImGui::InputText(Label, NameBuf, sizeof(NameBuf), ImGuiInputTextFlags_EnterReturnsTrue))
	{
		TargetObject->SetFName(FName(NameBuf));
	}
}
