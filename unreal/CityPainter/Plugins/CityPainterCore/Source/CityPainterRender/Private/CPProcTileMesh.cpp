// FCPProcTileMesh: the ICPTileMesh implementation on a UProceduralMeshComponent. THIS IS THE ONLY FILE in the
// plugin (besides the legacy road actor) that includes ProceduralMeshComponent.h. It must not know what the mesh
// represents. Collision is off (picking is analytic in the sim).

#include "ICPTileMesh.h"

#include "CityPainterRender.h"

#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"

namespace
{
class FCPProcTileMesh final : public ICPTileMesh
{
public:
	FCPProcTileMesh(AActor& Owner, FName DebugName)
	{
		UProceduralMeshComponent* C = NewObject<UProceduralMeshComponent>(&Owner, DebugName);
		C->bUseAsyncCooking = false;
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetupAttachment(Owner.GetRootComponent());
		C->RegisterComponent();
		Component = C;
	}

	virtual void SetSection(int32 SectionIndex, const FCPMeshSection& Data, UMaterialInterface* Material) override
	{
		UProceduralMeshComponent* C = Component.Get();
		if (!C)
		{
			return;
		}
		const int32 N = static_cast<int32>(Data.Position.size());
		TArray<FVector> Vertices, Normals;
		TArray<FVector2D> UV0;
		TArray<FProcMeshTangent> Tangents;
		TArray<FColor> Colours;  // empty: white
		TArray<int32> Indices;
		Vertices.Reserve(N);
		Normals.Reserve(N);
		UV0.Reserve(N);
		Tangents.Reserve(N);
		for (int32 i = 0; i < N; ++i)
		{
			Vertices.Add(FVector(Data.Position[i][0], Data.Position[i][1], Data.Position[i][2]));
			Normals.Add(FVector(Data.Normal[i][0], Data.Normal[i][1], Data.Normal[i][2]));
			UV0.Add(FVector2D(Data.UV0[i][0], Data.UV0[i][1]));
			Tangents.Add(FProcMeshTangent(FVector(Data.TangentX[i][0], Data.TangentX[i][1], Data.TangentX[i][2]), Data.bTangentFlipY));
		}
		Indices.Append(Data.Indices.data(), static_cast<int32>(Data.Indices.size()));
		C->SetWorldLocation(FVector(Data.OriginUeCm[0], Data.OriginUeCm[1], Data.OriginUeCm[2]));
		C->CreateMeshSection(SectionIndex, Vertices, Indices, Normals, UV0, Colours, Tangents, false);
		if (Material)
		{
			C->SetMaterial(SectionIndex, Material);
		}
	}

	virtual void Clear() override
	{
		if (UProceduralMeshComponent* C = Component.Get())
		{
			C->DestroyComponent();
		}
		Component.Reset();
	}

private:
	TWeakObjectPtr<UProceduralMeshComponent> Component;
};
}  // namespace

TUniquePtr<ICPTileMesh> ICPTileMesh::Create(AActor& Owner, FName DebugName)
{
	return MakeUnique<FCPProcTileMesh>(Owner, DebugName);
}
