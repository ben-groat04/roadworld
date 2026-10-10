#include "CPRoadNetworkActor.h"

#include "CPCoords.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "ProceduralMeshComponent.h"

#include <memory>
#include <utility>
#include <vector>

#include "cp/command.h"
#include "cp/road_commands.h"
#include "cp/road_graph.h"

struct FCPSimState
{
	cp::RoadGraph Graph;
	cp::CommandStack<cp::RoadGraph> Stack;
	FCPSimState() : Stack(Graph) {}
};

ACPRoadNetworkActor::ACPRoadNetworkActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	RoadMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RoadMesh"));
	RoadMesh->SetupAttachment(Root);
	RoadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Sim = MakeUnique<FCPSimState>();
}

ACPRoadNetworkActor::ACPRoadNetworkActor(FVTableHelper& Helper)
	: Super(Helper)
{
}

ACPRoadNetworkActor::~ACPRoadNetworkActor() = default;

void ACPRoadNetworkActor::BeginPlay()
{
	Super::BeginPlay();
	if (bRunSmokeTest)
	{
		RunSmokeTest();
	}
	RebuildVisuals();
}

static cp::Vec3 V3(double X, double Y, double Z)
{
	cp::Vec3 V;
	V.x = X;
	V.y = Y;
	V.z = Z;
	return V;
}

void ACPRoadNetworkActor::RunSmokeTest()
{
	if (!Sim)
	{
		return;
	}
	// Sim metres: x east, y north, z up.
	{
		cp::DrawRoadSpec S;
		S.start.pos = V3(0, 0, 0);
		S.end.pos = V3(200, 60, 6);
		S.interior.push_back(V3(100, -30, 3));
		S.road_type = "residential";
		Sim->Stack.execute(cp::make_draw_road(std::move(S)));
	}
	{
		cp::DrawRoadSpec S;
		S.start.pos = V3(0, 80, 0);
		S.end.pos = V3(250, 120, 0);
		S.road_type = "residential";
		Sim->Stack.execute(cp::make_draw_road(std::move(S)));
	}
	{
		cp::DrawRoadSpec S;
		S.start.pos = V3(50, 200, 0);
		S.end.pos = V3(250, 200, 0);
		S.interior.push_back(V3(150, 260, 0));
		S.road_type = "residential";
		S.one_way = true;
		Sim->Stack.execute(cp::make_draw_road(std::move(S)));
	}
}

bool ACPRoadNetworkActor::UndoLast()
{
	if (!Sim)
	{
		return false;
	}
	const bool bOk = Sim->Stack.undo();
	if (bOk)
	{
		RebuildVisuals();
	}
	return bOk;
}

bool ACPRoadNetworkActor::RedoLast()
{
	if (!Sim)
	{
		return false;
	}
	const bool bOk = Sim->Stack.redo();
	if (bOk)
	{
		RebuildVisuals();
	}
	return bOk;
}

void ACPRoadNetworkActor::RebuildVisuals()
{
	if (!Sim || !RoadMesh)
	{
		return;
	}
	UWorld* World = GetWorld();
#if ENABLE_DRAW_DEBUG
	if (World)
	{
		FlushPersistentDebugLines(World);
	}
#endif

	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;

	const double Spacing = FMath::Max(0.25, (double)SampleSpacingMetres);
	const double HalfWidthCm = 0.5 * RoadWidthMetres * CPCoords::MetresToCm;
	const FTransform ActorXf = GetActorTransform();

	for (const auto& Pair : Sim->Graph.edges())
	{
		const cp::Edge& E = Pair.second;
		const double Len = E.horizontal.length();
		if (Len <= 0.0)
		{
			continue;
		}
		const int32 Steps = FMath::Max(1, (int32)FMath::CeilToInt(Len / Spacing));

		TArray<FVector> Pts;
		Pts.Reserve(Steps + 1);
		for (int32 i = 0; i <= Steps; ++i)
		{
			const double S = Len * (double)i / (double)Steps;
			const cp::Vec3 P = cp::edge_point(E, S);
			Pts.Add(CPCoords::SimToUE(P.x, P.y, P.z));  // actor-local cm
		}

#if ENABLE_DRAW_DEBUG
		if (bDrawDebugLines && World)
		{
			for (int32 i = 0; i < Steps; ++i)
			{
				DrawDebugLine(World, ActorXf.TransformPosition(Pts[i]), ActorXf.TransformPosition(Pts[i + 1]),
					E.one_way ? FColor::Orange : FColor::Cyan, true, -1.f, 0, 12.f);
			}
		}
#endif

		// Ribbon: two verts per sample (left, right of travel). Right of travel in UE is (-ty, tx).
		const int32 Base = Verts.Num();
		double Dist = 0.0;
		for (int32 i = 0; i <= Steps; ++i)
		{
			const FVector A = Pts[FMath::Max(0, i - 1)];
			const FVector B = Pts[FMath::Min(Steps, i + 1)];
			FVector2D T(B.X - A.X, B.Y - A.Y);
			if (!T.Normalize())
			{
				T = FVector2D(1.0, 0.0);
			}
			const FVector Right(-T.Y, T.X, 0.0);
			Verts.Add(Pts[i] - Right * HalfWidthCm);
			Verts.Add(Pts[i] + Right * HalfWidthCm);
			Normals.Add(FVector::UpVector);
			Normals.Add(FVector::UpVector);
			if (i > 0)
			{
				Dist += FVector::Dist(Pts[i], Pts[i - 1]);
			}
			const double V = Dist / (RoadWidthMetres * CPCoords::MetresToCm);
			UVs.Add(FVector2D(0.0, V));
			UVs.Add(FVector2D(1.0, V));
			Colors.Add(FLinearColor::White);
			Colors.Add(FLinearColor::White);
			Tangents.Add(FProcMeshTangent(T.X, T.Y, 0.f));
			Tangents.Add(FProcMeshTangent(T.X, T.Y, 0.f));
		}
		// Clockwise seen from above (UE front face), with indices L0,R0,L1,R1 = 0,1,2,3.
		for (int32 i = 0; i < Steps; ++i)
		{
			const int32 L0 = Base + 2 * i, R0 = L0 + 1, L1 = L0 + 2, R1 = L0 + 3;
			Tris.Add(L0); Tris.Add(L1); Tris.Add(R1);
			Tris.Add(L0); Tris.Add(R1); Tris.Add(R0);
		}
	}

	RoadMesh->ClearAllMeshSections();
	if (Verts.Num() > 0)
	{
		RoadMesh->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, Tangents, false);
		if (RoadMaterial)
		{
			RoadMesh->SetMaterial(0, RoadMaterial);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("CPRoadNetwork: %d edges, %d verts"), (int32)Sim->Graph.edges().size(), Verts.Num());
}
