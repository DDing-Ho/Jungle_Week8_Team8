#include "BoxComponent.h"

#include "Core/PropertyTypes.h"
#include "Engine/Serialization/Archive.h"

DEFINE_CLASS(UBoxComponent, UShapeComponent)

UBoxComponent::UBoxComponent()
{
     CollisionType = ECollisionType::Box;
}

void UBoxComponent::GetEditableProperties(TArray<FPropertyDescriptor>& OutProps)
{
    UShapeComponent::GetEditableProperties(OutProps);
    OutProps.push_back({ "Box Extent", EPropertyType::Vec3, &BoxExtent });
}

void UBoxComponent::PostEditProperty(const char* PropertyName)
{
    UShapeComponent::PostEditProperty(PropertyName);
    UpdateBodySetup();
}

void UBoxComponent::Serialize(FArchive& Ar)
{
    UShapeComponent::Serialize(Ar);
    Ar << "BoxExtent" << BoxExtent;
}

void UBoxComponent::UpdateWorldAABB() const
{
    const FVector SafeExtent(std::fabs(BoxExtent.X), std::fabs(BoxExtent.Y), std::fabs(BoxExtent.Z));
    const FAABB LocalAABB(-SafeExtent, SafeExtent);
    WorldAABB = FAABB::TransformAABB(LocalAABB, GetWorldMatrix());
}

// RayCasting 전용 AABB
bool UBoxComponent::RaycastMesh(const FRay& Ray, FHitResult& OutHitResult)
{
    float HitT = 0.0f;
    if (!GetWorldAABB().IntersectRay(Ray, HitT))
    {
        return false;
    }

    OutHitResult.HitComponent = this;
    OutHitResult.Distance = HitT;
    OutHitResult.Location = Ray.Origin + (Ray.Direction * HitT);
    OutHitResult.bHit = true;
    return true;
}

void UBoxComponent::SetBoxExtent(const FVector& InBoxExtent)
{
    BoxExtent = InBoxExtent;
    UpdateBodySetup();
}
