// UHT requires the actor header first. The build header (Sim module, std-only) follows it; sim_core headers are NOT
// included here, everything sim-facing is in Sim/Private/Legacy/CPRoadMeshBuild.cpp.
#include "CPRoadMeshActor.h"

#include "Legacy/CPRoadMeshBuild.h"

#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MaterialDomain.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"

DEFINE_LOG_CATEGORY(LogCPRoadMesh);

// Everything the actor keeps that needs sim types (opaque here).
struct FCPMeshSim
{
	CPBuild::FWorldPtr World;
	CPBuild::FOutput Last;  // last build output (debug lines, stats)

	struct FBinding
	{
		TWeakObjectPtr<UProceduralMeshComponent> Comp;
		int32 SectionIndex = 0;
		FName Key;
		bool bMarking = false;
		float Rgb[3] = {1.f, 1.f, 1.f};
	};
	TArray<FBinding> Bindings;
};

namespace
{

std::string ToStd(const FString& S)
{
	FTCHARToUTF8 Conv(*S);
	return std::string(Conv.Get(), Conv.Length());
}

FString FromStd(const std::string& S)
{
	return FString(UTF8_TO_TCHAR(S.c_str()));
}

// Surface class -> texture slot (unreal/tools/py/import_textures.py, manifest slots under
// /Game/Materials/Surfaces/<slot>/MI_<slot>). Classes come from data/lane_types.json "surface" plus the mesh
// builder's island_surface ("island"); unknown classes try a slot of the same name.
FString SlotForSurface(const FString& SurfaceClass)
{
	if (SurfaceClass == TEXT("asphalt")) return TEXT("asphalt_new");
	if (SurfaceClass == TEXT("paving")) return TEXT("pavement");
	if (SurfaceClass == TEXT("kerb")) return TEXT("kerb_concrete");
	if (SurfaceClass == TEXT("grass")) return TEXT("grass");
	if (SurfaceClass == TEXT("island")) return TEXT("grass");  // island tops: planted/turfed by default
	return SurfaceClass;
}

UMaterialInterface* TryLoadMaterial(const FString& PackagePathAndName)
{
	// "/Game/Materials/Surfaces/x/MI_x" -> object path "/Game/Materials/Surfaces/x/MI_x.MI_x"
	const FString ObjectName = FPaths::GetBaseFilename(PackagePathAndName);
	const FString ObjectPath = PackagePathAndName + TEXT(".") + ObjectName;
	return LoadObject<UMaterialInterface>(nullptr, *ObjectPath, nullptr, LOAD_NoWarn | LOAD_Quiet);
}

void CreateSection(UProceduralMeshComponent* Comp, int32 SectionIndex, const CPBuild::FSection& S)
{
	const int32 N = static_cast<int32>(S.Position.size());
	TArray<FVector> Verts;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FVector2D> UV1;
	TArray<FColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	Verts.Reserve(N);
	Normals.Reserve(N);
	UV0.Reserve(N);
	UV1.Reserve(static_cast<int32>(S.UV1.size()));
	Colors.Reserve(N);
	Tangents.Reserve(N);
	for (int32 i = 0; i < N; ++i)
	{
		Verts.Add(FVector(S.Position[i].X, S.Position[i].Y, S.Position[i].Z));
		Normals.Add(FVector(S.Normal[i].X, S.Normal[i].Y, S.Normal[i].Z));
		UV0.Add(FVector2D(S.UV0[i].X, S.UV0[i].Y));
		if (i < static_cast<int32>(S.UV1.size()))
		{
			UV1.Add(FVector2D(S.UV1[i].X, S.UV1[i].Y));  // lane-relative: x = metres from lane centre, y = lane width
		}
		Colors.Add(FColor(S.Colour[i][0], S.Colour[i][1], S.Colour[i][2], S.Colour[i][3]));
		Tangents.Add(FProcMeshTangent(FVector(S.TangentX[i].X, S.TangentX[i].Y, S.TangentX[i].Z), S.TangentFlipY[i] != 0));
	}
	TArray<int32> Tris;
	Tris.Reserve(static_cast<int32>(S.Indices.size()));
	for (std::int32_t Idx : S.Indices)
	{
		Tris.Add(Idx);
	}
	// Winding follows the rule in CPRoadMeshBuild.h (indices unchanged from sim); do not reorder here.
	TArray<FVector2D> NoUV;
	Comp->CreateMeshSection(SectionIndex, Verts, Tris, Normals, UV0, UV1, NoUV, NoUV, Colors, Tangents, /*bCreateCollision=*/false);
}

}  // namespace

ACPRoadMeshActor::ACPRoadMeshActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = false;
	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SceneRoot);
	Sim = MakeUnique<FCPMeshSim>();
}

ACPRoadMeshActor::ACPRoadMeshActor(FVTableHelper& Helper)
	: Super(Helper)
{
}

ACPRoadMeshActor::~ACPRoadMeshActor() = default;

void ACPRoadMeshActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Not for the class default object, Blueprint preview/template actors, or worlds without a real scene.
	if (IsTemplate() || HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
	{
		return;
	}
	UWorld* World = GetWorld();
	if (!World || World->WorldType == EWorldType::EditorPreview || World->WorldType == EWorldType::Inactive)
	{
		return;
	}
	// Game worlds (including PIE) build in BeginPlay. In the editor world this makes the roads visible without
	// pressing Play; it only does work when a setting changed (dragging the actor just redraws debug lines).
	if (!World->IsGameWorld())
	{
		EnsureBuilt();
	}
}

void ACPRoadMeshActor::BeginPlay()
{
	Super::BeginPlay();
	EnsureBuilt();
}

void ACPRoadMeshActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}

uint32 ACPRoadMeshActor::ComputeSimHash() const
{
	uint32 H = GetTypeHash(DataDirOverride.Path);
	H = HashCombine(H, GetTypeHash(RegionId));
	H = HashCombine(H, GetTypeHash(static_cast<int32>(Layout)));
	return H;
}

uint32 ACPRoadMeshActor::ComputeRenderHash() const
{
	uint32 H = ComputeSimHash();
	H = HashCombine(H, GetTypeHash(FMath::RoundToInt(TileSizeMetres * 100.f)));
	H = HashCombine(H, GetTypeHash(FMath::RoundToInt(ChordToleranceMetres * 10000.f)));
	H = HashCombine(H, GetTypeHash(static_cast<int32>(bGenerateMarkings)));
	H = HashCombine(H, GetTypeHash(FMath::RoundToInt(MarkingLiftMillimetres * 10.f)));
	H = HashCombine(H, GetTypeHash(static_cast<int32>(bDrawConnectorPaths)));
	H = HashCombine(H, GetTypeHash(static_cast<int32>(bDrawTrimLines)));
	H = HashCombine(H, GetTypeHash(static_cast<int32>(bDrawLaneCentrelines)));
	H = HashCombine(H, GetTypeHash(static_cast<int32>(bShowDegradedJunctions)));
	return H;
}

void ACPRoadMeshActor::EnsureBuilt()
{
	if (!Sim)
	{
		return;
	}
	const uint32 SimHash = ComputeSimHash();
	const uint32 RenderHash = ComputeRenderHash();
	if (!bBuilt || SimHash != LastSimHash || !Sim->World)
	{
		RebuildAll();
	}
	else if (RenderHash != LastRenderHash)
	{
		RefreshFromSim();
	}
	else
	{
		// Nothing sim- or render-affecting changed (e.g. the actor was dragged, or a material was assigned).
		ApplyMaterials();
		DrawDebug();
	}
}

bool ACPRoadMeshActor::ResolveDataDir(FString& OutDir)
{
	TArray<FString> Tried;
	auto Try = [&](const FString& Candidate) -> bool
	{
		if (Candidate.IsEmpty())
		{
			return false;
		}
		const FString Full = FPaths::ConvertRelativePathToFull(Candidate);
		FString Collapsed = Full;
		FPaths::CollapseRelativeDirectories(Collapsed);
		FPaths::NormalizeDirectoryName(Collapsed);
		Tried.Add(Collapsed);
		if (IFileManager::Get().FileExists(*FPaths::Combine(Collapsed, TEXT("lane_types.json"))))
		{
			OutDir = Collapsed;
			return true;
		}
		return false;
	};

	if (Try(DataDirOverride.Path))
	{
		return true;
	}
	// <repo>/unreal/CityPainter/Plugins/CityPainterCore -> up four levels = <repo> (Build.cs goes up six from the module dir).
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CityPainterCore"));
	if (Plugin.IsValid() && Try(FPaths::Combine(Plugin->GetBaseDir(), TEXT("../../../../data"))))
	{
		return true;
	}
	// <repo>/unreal/CityPainter (project dir) -> up two levels.
	if (Try(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../data"))))
	{
		return true;
	}
	UE_LOG(LogCPRoadMesh, Error, TEXT("Cannot find the data directory (needs lane_types.json). Tried: %s. Set DataDirOverride on the actor."),
		*FString::Join(Tried, TEXT("; ")));
	return false;
}

void ACPRoadMeshActor::RebuildAll()
{
	if (!Sim)
	{
		return;
	}
	if (IsTemplate())
	{
		return;
	}
	FString DataDir;
	if (!ResolveDataDir(DataDir))
	{
		DestroyTileComponents();
		bBuilt = false;
		return;
	}

	CPBuild::FSettings S;
	S.DataDir = ToStd(DataDir);
	S.RegionId = ToStd(RegionId);
	S.Layout = (Layout == ECPTestLayout::Empty) ? CPBuild::ELayout::Empty : CPBuild::ELayout::Interchange;

	Sim->World = CPBuild::CreateWorld();
	std::string Error, Warning;
	if (!CPBuild::LoadData(*Sim->World, S, Error, Warning))
	{
		UE_LOG(LogCPRoadMesh, Error, TEXT("Loading region data failed: %s"), *FromStd(Error));
		Sim->World.reset();
		DestroyTileComponents();
		bBuilt = false;
		return;
	}
	if (!Warning.empty())
	{
		UE_LOG(LogCPRoadMesh, Warning, TEXT("%s"), *FromStd(Warning));
	}
	if (!CPBuild::BuildLayout(*Sim->World, S.Layout, Error))
	{
		UE_LOG(LogCPRoadMesh, Error, TEXT("Building layout failed: %s"), *FromStd(Error));
		Sim->World.reset();
		DestroyTileComponents();
		bBuilt = false;
		return;
	}
	UE_LOG(LogCPRoadMesh, Log, TEXT("Data dir %s, region %s"), *DataDir, *RegionId);
	LastSimHash = ComputeSimHash();
	bBuilt = true;
	RefreshFromSim();
}

void ACPRoadMeshActor::RefreshFromSim()
{
	if (!Sim || !Sim->World)
	{
		return;
	}
	FString DataDir;
	if (!ResolveDataDir(DataDir))
	{
		return;
	}
	CPBuild::FSettings S;
	S.DataDir = ToStd(DataDir);
	S.RegionId = ToStd(RegionId);
	S.TileSizeM = FMath::Max(32.0, static_cast<double>(TileSizeMetres));
	S.ChordToleranceM = FMath::Max(0.002, static_cast<double>(ChordToleranceMetres));
	S.bGenerateMarkings = bGenerateMarkings;
	S.MarkingLiftCm = static_cast<double>(MarkingLiftMillimetres) * 0.1;
	S.bDebugConnectors = bDrawConnectorPaths;
	S.bDebugTrimLines = bDrawTrimLines;
	S.bDebugLaneCentrelines = bDrawLaneCentrelines;
	S.bDebugDegraded = bShowDegradedJunctions;

	const double T0 = FPlatformTime::Seconds();
	Sim->Last = CPBuild::BuildRender(*Sim->World, S);
	const double T1 = FPlatformTime::Seconds();
	const CPBuild::FOutput& Out = Sim->Last;

	// ---- upload ----
	DestroyTileComponents();
	Sim->Bindings.Reset();
	int32 Sections = 0;
	for (const CPBuild::FTile& Tile : Out.Tiles)
	{
		const FVector Origin(Tile.Origin[0], Tile.Origin[1], Tile.Origin[2]);
		for (int32 Pass = 0; Pass < 2; ++Pass)
		{
			const bool bMarking = (Pass == 1);
			const std::vector<CPBuild::FSection>& List = bMarking ? Tile.Markings : Tile.Surfaces;
			if (List.empty())
			{
				continue;
			}
			UProceduralMeshComponent* Comp = NewObject<UProceduralMeshComponent>(this, NAME_None, RF_Transient);
			Comp->SetupAttachment(GetRootComponent());
			Comp->SetRelativeLocation(Origin);  // double precision; vertices are tile-relative floats
			Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Comp->bUseAsyncCooking = false;
			if (bMarking)
			{
				Comp->SetCastShadow(false);
			}
			Comp->RegisterComponent();
			int32 SectionIndex = 0;
			for (const CPBuild::FSection& Sec : List)
			{
				CreateSection(Comp, SectionIndex, Sec);
				FCPMeshSim::FBinding B;
				B.Comp = Comp;
				B.SectionIndex = SectionIndex;
				B.Key = FName(*FromStd(Sec.Key));
				B.bMarking = bMarking;
				B.Rgb[0] = Sec.Rgb[0];
				B.Rgb[1] = Sec.Rgb[1];
				B.Rgb[2] = Sec.Rgb[2];
				Sim->Bindings.Add(B);
				++SectionIndex;
				++Sections;
			}
			TileComponents.Add(Comp);
		}
	}
	ApplyMaterials();
	DrawDebug();
	LastRenderHash = ComputeRenderHash();

	// ---- log ----
	const CPBuild::FStats& St = Out.Stats;
	UE_LOG(LogCPRoadMesh, Log, TEXT("Built in %.0f ms: %d nodes, %d edges, %d junctions (%d roundabout ring nodes, %d degraded), %d roundabouts, %d tiles, %d sections, %d marks"),
		(T1 - T0) * 1000.0, (int32)St.Nodes, (int32)St.Edges, (int32)St.Junctions, (int32)St.RoundaboutRingJunctions,
		(int32)St.DegradedJunctions, (int32)St.Roundabouts, (int32)St.Tiles, Sections, (int32)St.Marks);
	for (const auto& Pair : St.TrianglesBySurface)
	{
		UE_LOG(LogCPRoadMesh, Log, TEXT("  surface %s: %d triangles"), *FromStd(Pair.first), (int32)Pair.second);
	}
	for (const auto& Pair : St.TrianglesByMarkingColour)
	{
		UE_LOG(LogCPRoadMesh, Log, TEXT("  marking %s: %d triangles"), *FromStd(Pair.first), (int32)Pair.second);
	}
	for (const std::string& Msg : Out.Messages)
	{
		UE_LOG(LogCPRoadMesh, Warning, TEXT("  %s"), *FromStd(Msg));
	}
}

void ACPRoadMeshActor::DestroyTileComponents()
{
	for (TObjectPtr<UProceduralMeshComponent>& C : TileComponents)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	TileComponents.Reset();
	if (Sim)
	{
		Sim->Bindings.Reset();
	}
}

UMaterialInterface* ACPRoadMeshActor::ResolveSurfaceMaterial(FName SurfaceClass)
{
	if (const TObjectPtr<UMaterialInterface>* Assigned = SurfaceMaterials.Find(SurfaceClass))
	{
		if (*Assigned)
		{
			return *Assigned;
		}
	}
	if (const TObjectPtr<UMaterialInterface>* Cached = ResolvedSurfaceMaterials.Find(SurfaceClass))
	{
		if (*Cached)
		{
			return *Cached;
		}
	}
	const FString Slot = SlotForSurface(SurfaceClass.ToString());
	const FString Path = FString::Printf(TEXT("/Game/Materials/Surfaces/%s/MI_%s"), *Slot, *Slot);
	UMaterialInterface* Mat = TryLoadMaterial(Path);
	if (Mat)
	{
		UE_LOG(LogCPRoadMesh, Log, TEXT("Surface '%s' -> %s (slot %s)"), *SurfaceClass.ToString(), *Path, *Slot);
	}
	else
	{
		Mat = UMaterial::GetDefaultMaterial(MD_Surface);
		UE_LOG(LogCPRoadMesh, Log, TEXT("Surface '%s' -> engine default material (%s not found; run import_textures.py or assign SurfaceMaterials)"),
			*SurfaceClass.ToString(), *Path);
	}
	ResolvedSurfaceMaterials.Add(SurfaceClass, Mat);
	return Mat;
}

UMaterialInterface* ACPRoadMeshActor::ResolveMarkingMaterial(FName ColourId, const float Rgb[3])
{
	if (const TObjectPtr<UMaterialInterface>* Assigned = MarkingMaterials.Find(ColourId))
	{
		if (*Assigned)
		{
			return *Assigned;
		}
	}
	if (const TObjectPtr<UMaterialInterface>* Cached = ResolvedMarkingMaterials.Find(ColourId))
	{
		if (*Cached)
		{
			return *Cached;
		}
	}
	const FString Path = FString::Printf(TEXT("/Game/Materials/Markings/MI_marking_%s"), *ColourId.ToString());
	UMaterialInterface* Mat = TryLoadMaterial(Path);
	if (Mat)
	{
		UE_LOG(LogCPRoadMesh, Log, TEXT("Marking colour '%s' -> %s"), *ColourId.ToString(), *Path);
	}
	else
	{
		// Tinted engine material: BasicShapeMaterial has a vector parameter "Color" (VERIFY; falls back to untinted).
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"), nullptr, LOAD_NoWarn | LOAD_Quiet);
		if (!Base)
		{
			Base = UMaterial::GetDefaultMaterial(MD_Surface);
		}
		UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, this);
		if (Mid)
		{
			const FColor Srgb(FMath::RoundToInt(Rgb[0] * 255.f), FMath::RoundToInt(Rgb[1] * 255.f), FMath::RoundToInt(Rgb[2] * 255.f));
			Mid->SetVectorParameterValue(TEXT("Color"), FLinearColor::FromSRGBColor(Srgb));
			GeneratedMaterials.Add(Mid);
			Mat = Mid;
		}
		else
		{
			Mat = Base;
		}
		UE_LOG(LogCPRoadMesh, Log, TEXT("Marking colour '%s' -> generated tinted engine material (%s not found; run setup_look_test_level.py or assign MarkingMaterials)"),
			*ColourId.ToString(), *Path);
	}
	ResolvedMarkingMaterials.Add(ColourId, Mat);
	return Mat;
}

void ACPRoadMeshActor::ApplyMaterials()
{
	if (!Sim)
	{
		return;
	}
	for (const FCPMeshSim::FBinding& B : Sim->Bindings)
	{
		UProceduralMeshComponent* Comp = B.Comp.Get();
		if (!Comp)
		{
			continue;
		}
		UMaterialInterface* Mat = B.bMarking ? ResolveMarkingMaterial(B.Key, B.Rgb) : ResolveSurfaceMaterial(B.Key);
		if (Mat)
		{
			Comp->SetMaterial(B.SectionIndex, Mat);
		}
	}
}

void ACPRoadMeshActor::DrawDebug()
{
#if ENABLE_DRAW_DEBUG
	UWorld* World = GetWorld();
	if (!World || !Sim)
	{
		return;
	}
	// Flushes persistent debug lines of every actor in this world, not only ours.
	FlushPersistentDebugLines(World);
	const FTransform Xf = GetActorTransform();
	for (const CPBuild::FDebugLine& L : Sim->Last.DebugLines)
	{
		FColor Colour = FColor::Cyan;
		float Thickness = 6.f;
		switch (L.Kind)
		{
		case CPBuild::EDebugKind::Connector: Colour = FColor::Cyan; break;
		case CPBuild::EDebugKind::TrimLine: Colour = FColor::Yellow; break;
		case CPBuild::EDebugKind::LaneCentre: Colour = FColor::Green; Thickness = 3.f; break;
		case CPBuild::EDebugKind::DegradedJunction: Colour = FColor::Red; Thickness = 14.f; break;
		}
		for (size_t i = 0; i + 1 < L.Points.size(); ++i)
		{
			const FVector A = Xf.TransformPosition(FVector(L.Points[i][0], L.Points[i][1], L.Points[i][2]));
			const FVector B = Xf.TransformPosition(FVector(L.Points[i + 1][0], L.Points[i + 1][1], L.Points[i + 1][2]));
			DrawDebugLine(World, A, B, Colour, /*bPersistentLines=*/true, /*LifeTime=*/-1.f, /*DepthPriority=*/0, Thickness);
		}
	}
#endif
}

bool ACPRoadMeshActor::UndoLast()
{
	if (!Sim || !Sim->World)
	{
		return false;
	}
	const bool bOk = CPBuild::Undo(*Sim->World);
	if (bOk)
	{
		RefreshFromSim();  // TODO(dirty tiles): rebuild only the tiles touched by the command
	}
	return bOk;
}

bool ACPRoadMeshActor::RedoLast()
{
	if (!Sim || !Sim->World)
	{
		return false;
	}
	const bool bOk = CPBuild::Redo(*Sim->World);
	if (bOk)
	{
		RefreshFromSim();
	}
	return bOk;
}
