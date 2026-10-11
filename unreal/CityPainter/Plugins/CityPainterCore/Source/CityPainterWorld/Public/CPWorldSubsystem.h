#pragma once

// UCPWorldSubsystem: owns the simulation world (FCPSimWorld) for one UWorld and is the ONLY adapter between Unreal
// types and the sim: it turns Unreal-frame rays and points into sim queries through CPFrame.h and back. It exists
// only in worlds on a City Painter map (UCPSettings::MapPathPrefix). Later it will also own command submission, undo
// and the worker that updates the world cache. It must not know about meshes, materials, actors, input or UI.

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "CPSimWorld.h"

#include "CPWorldSubsystem.generated.h"

/** Result of a ground pick, in Unreal's frame (location, centimetres) plus the sim values a HUD wants. */
struct FCPUEGroundPick
{
	FVector LocationCm = FVector::ZeroVector;
	double SimX = 0, SimY = 0;     // metres east / north of the sim origin
	double HeightM = 0;
	double SlopeRiseRun = 0;
};

UCLASS()
class CITYPAINTERWORLD_API UCPWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UCPWorldSubsystem();
	~UCPWorldSubsystem();

	// UWorldSubsystem
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	// FTickableGameObject
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return true; }

	/** The simulation, or null when it failed to start (see the log). */
	FCPSimWorld* GetSim() const { return Sim.Get(); }

	/** Picks the terrain along a ray given in Unreal's frame (centimetres). Analytic, no collision. */
	bool PickGroundUE(const FVector& OriginCm, const FVector& DirectionUE, FCPUEGroundPick& Out) const;

	/** Terrain height in Unreal centimetres below/above an Unreal-frame position (X, Y in cm). 0 without a sim. */
	double GroundZCm(double XCm, double YCm) const;

	/** Terrain height in metres at a sim position (x east, y north). For editor scripts and tests; 0 without a sim. */
	UFUNCTION(BlueprintCallable, Category = "CityPainter")
	double GetTerrainHeightM(double SimX, double SimY) const;

	/** Terrain extent in Unreal centimetres (X north, Y east); an invalid box without a sim. */
	FBox2D WorldBoundsCm() const;

private:
	TUniquePtr<FCPSimWorld> Sim;
};
