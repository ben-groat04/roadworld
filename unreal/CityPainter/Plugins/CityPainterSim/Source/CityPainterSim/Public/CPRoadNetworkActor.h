#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "CPRoadNetworkActor.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
struct FCPSimState;

/**
 * Owns a cp::RoadGraph and cp::CommandStack (sim_core) and renders the graph:
 * debug lines per edge, plus a flat ribbon mesh sampled from each edge's clothoid chain.
 * Coordinates: see CPCoords.h. Actor-local space = sim space converted to UE cm.
 */
UCLASS()
class CITYPAINTERSIM_API ACPRoadNetworkActor : public AActor
{
	GENERATED_BODY()

public:
	ACPRoadNetworkActor(const FObjectInitializer& ObjectInitializer);
	// Out-of-line (.cpp): UHT's generated FVTableHelper constructor would otherwise instantiate
	// ~TUniquePtr<FCPSimState> against the incomplete type.
	ACPRoadNetworkActor(FVTableHelper& Helper);
	virtual ~ACPRoadNetworkActor() override;

	virtual void BeginPlay() override;
	/** Editor worlds only: builds the smoke-test roads so they are visible without pressing Play. */
	virtual void OnConstruction(const FTransform& Transform) override;

	/** Undo the last command. Returns false if nothing to undo. */
	UFUNCTION(BlueprintCallable, Category = "CityPainter")
	bool UndoLast();

	UFUNCTION(BlueprintCallable, Category = "CityPainter")
	bool RedoLast();

	/** Re-sample the graph and rebuild debug lines + ribbon mesh. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "CityPainter")
	void RebuildVisuals();

	/** Ribbon width in metres (one fixed width for the smoke test). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter", meta = (ClampMin = "0.5"))
	float RoadWidthMetres = 7.0f;

	/** Distance between samples along an edge, in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter", meta = (ClampMin = "0.25"))
	float SampleSpacingMetres = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter")
	bool bDrawDebugLines = true;

	/** Run the hard-coded DrawRoad smoke test in BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter")
	bool bRunSmokeTest = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter")
	TObjectPtr<UMaterialInterface> RoadMaterial;

private:
	UPROPERTY(VisibleAnywhere, Category = "CityPainter")
	TObjectPtr<UProceduralMeshComponent> RoadMesh;

	// pimpl so sim_core headers stay out of this header.
	TUniquePtr<FCPSimState> Sim;

	void RunSmokeTest();
};
