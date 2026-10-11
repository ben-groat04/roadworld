#include "CPWorldSubsystem.h"

#include "CPSettings.h"
#include "CityPainterWorld.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformTime.h"
#include "Misc/PackageName.h"

namespace
{
std::string ToStd(const FString& S)
{
	const FTCHARToUTF8 Utf8(*S);
	return std::string(Utf8.Get(), Utf8.Length());
}
}  // namespace

UCPWorldSubsystem::UCPWorldSubsystem() = default;
UCPWorldSubsystem::~UCPWorldSubsystem() = default;

bool UCPWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE || WorldType == EWorldType::Editor;
}

bool UCPWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	const UWorld* World = Cast<UWorld>(Outer);
	if (!World)
	{
		return false;
	}
	const FString Package = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	return Package.StartsWith(GetDefault<UCPSettings>()->MapPathPrefix);
}

void UCPWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	const UCPSettings* Settings = GetDefault<UCPSettings>();
	FCPSimWorldConfig Config;
	Config.DataDir = ToStd(Settings->ResolveDataDir());
	Config.RegionId = ToStd(Settings->RegionId);
	Config.Seed = static_cast<uint64>(Settings->WorldSeed);
	Config.SizeM = Settings->WorldSizeM;
	Config.CellM = Settings->CellSizeM;
	Config.bVegetationLayers = Settings->bRegisterVegetationLayers;

	const double T0 = FPlatformTime::Seconds();
	std::string Error, Warning;
	Sim = TUniquePtr<FCPSimWorld>(FCPSimWorld::Create(Config, Error, Warning).release());
	if (!Sim)
	{
		UE_LOG(LogCPWorld, Error, TEXT("Sim world failed to start: %hs"), Error.c_str());
		return;
	}
	if (!Warning.empty())
	{
		UE_LOG(LogCPWorld, Warning, TEXT("%hs"), Warning.c_str());
	}
	const FCPTerrainGrid& G = Sim->TerrainGrid();
	UE_LOG(LogCPWorld, Log, TEXT("Sim world started in %.0f ms: seed %lld, %.0f m square, %dx%d samples at %.1f m, region '%s', data '%hs'"),
		(FPlatformTime::Seconds() - T0) * 1000.0, Settings->WorldSeed, G.MaxX - G.MinX, G.Nx, G.Ny, G.CellM, *Settings->RegionId, Config.DataDir.c_str());
	if (Sim->HasRoads())
	{
		UE_LOG(LogCPWorld, Log, TEXT("Roads ready: %d road types, first world-cache build %.0f ms"), static_cast<int32>(Sim->RoadTypes().size()), Sim->PrimeMs());
	}
	else
	{
		UE_LOG(LogCPWorld, Warning, TEXT("Roads unavailable: %hs"), Sim->RoadsUnavailableReason().c_str());
	}
}

FCPEditResult UCPWorldSubsystem::Submit(FCPCommandHandle Command)
{
	if (!Sim)
	{
		FCPEditResult R;
		R.Error = "no sim world";
		return Publish(TEXT("submit"), MoveTemp(R));
	}
	const FString Name = CPToolBridge::ToFString(CPCommandName(Command));
	return Publish(*FString::Printf(TEXT("submit '%s'"), *Name), Sim->Submit(MoveTemp(Command)));
}

FCPEditResult UCPWorldSubsystem::Undo()
{
	return Publish(TEXT("undo"), Sim ? Sim->Undo() : FCPEditResult{false, "no sim world", {}, {}, 0.0});
}

FCPEditResult UCPWorldSubsystem::Redo()
{
	return Publish(TEXT("redo"), Sim ? Sim->Redo() : FCPEditResult{false, "no sim world", {}, {}, 0.0});
}

FCPEditResult UCPWorldSubsystem::Publish(const TCHAR* What, FCPEditResult Result)
{
	if (!Result.bOk)
	{
		UE_LOG(LogCPWorld, Warning, TEXT("Edit '%s' failed: %hs"), What, Result.Error.c_str());
		return Result;
	}
	for (const std::string& Warning : Result.Changes.Warnings)
	{
		UE_LOG(LogCPWorld, Warning, TEXT("Edit '%s': %hs"), What, Warning.c_str());
	}
	UE_LOG(LogCPWorld, Log, TEXT("Edit '%s': world v%llu, %d nodes %d edges, road tiles +%d ~%d -%d, marking tiles +%d ~%d -%d, %.1f ms"), What,
		static_cast<unsigned long long>(Result.Changes.WorldVersion), static_cast<int32>(Result.Graph.Nodes), static_cast<int32>(Result.Graph.Edges),
		static_cast<int32>(Result.Changes.Count(CPLayer::RoadMesh, ECPTileChange::Added)), static_cast<int32>(Result.Changes.Count(CPLayer::RoadMesh, ECPTileChange::Changed)),
		static_cast<int32>(Result.Changes.Count(CPLayer::RoadMesh, ECPTileChange::Removed)), static_cast<int32>(Result.Changes.Count(CPLayer::Markings, ECPTileChange::Added)),
		static_cast<int32>(Result.Changes.Count(CPLayer::Markings, ECPTileChange::Changed)), static_cast<int32>(Result.Changes.Count(CPLayer::Markings, ECPTileChange::Removed)),
		Result.UpdateMs);
	if (!Result.Changes.IsEmpty())
	{
		TilesChanged.Broadcast(Result.Changes);
	}
	return Result;
}

void UCPWorldSubsystem::Deinitialize()
{
	Sim.Reset();
	Super::Deinitialize();
}

void UCPWorldSubsystem::Tick(float DeltaTime)
{
	// Reserved: pumps results of the sim worker once edits exist (milestone M4). Nothing to do in M1.
}

TStatId UCPWorldSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UCPWorldSubsystem, STATGROUP_Tickables);
}

bool UCPWorldSubsystem::PickGroundUE(const FVector& OriginCm, const FVector& DirectionUE, FCPUEGroundPick& Out) const
{
	if (!Sim)
	{
		return false;
	}
	const CPFrame::FD3 O = CPFrame::UEToSim({OriginCm.X, OriginCm.Y, OriginCm.Z});
	const CPFrame::FD3 D = CPFrame::UEDirToSim({DirectionUE.X, DirectionUE.Y, DirectionUE.Z});
	FCPGroundPick Pick;
	if (!Sim->PickGround(O, D, Pick))
	{
		return false;
	}
	const CPFrame::FD3 U = CPFrame::SimToUE(Pick.PointSim);
	Out.LocationCm = FVector(U.X, U.Y, U.Z);
	Out.SimX = Pick.PointSim.X;
	Out.SimY = Pick.PointSim.Y;
	Out.HeightM = Pick.HeightM;
	Out.SlopeRiseRun = Pick.SlopeRiseRun;
	return true;
}

double UCPWorldSubsystem::GroundZCm(double XCm, double YCm) const
{
	if (!Sim)
	{
		return 0.0;
	}
	const CPFrame::FD3 S = CPFrame::UEToSim({XCm, YCm, 0.0});
	return CPFrame::SimToUE({S.X, S.Y, Sim->HeightM(S.X, S.Y)}).Z;
}

FBox2D UCPWorldSubsystem::WorldBoundsCm() const
{
	if (!Sim)
	{
		return FBox2D(ForceInit);
	}
	const FCPTerrainGrid& G = Sim->TerrainGrid();
	const CPFrame::FD3 A = CPFrame::SimToUE({G.MinX, G.MinY, 0.0}), B = CPFrame::SimToUE({G.MaxX, G.MaxY, 0.0});
	return FBox2D(FVector2D(FMath::Min(A.X, B.X), FMath::Min(A.Y, B.Y)), FVector2D(FMath::Max(A.X, B.X), FMath::Max(A.Y, B.Y)));
}

double UCPWorldSubsystem::GetTerrainHeightM(const UObject* WorldContextObject, double SimX, double SimY)
{
	const UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	const UCPWorldSubsystem* Sub = World ? World->GetSubsystem<UCPWorldSubsystem>() : nullptr;
	return Sub && Sub->Sim ? Sub->Sim->HeightM(SimX, SimY) : 0.0;
}
