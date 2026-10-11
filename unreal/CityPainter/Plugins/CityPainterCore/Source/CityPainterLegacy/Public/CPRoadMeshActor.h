#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "GameFramework/Actor.h"
#include "CPRoadMeshActor.generated.h"

class UMaterialInterface;
class USceneComponent;
class UProceduralMeshComponent;
struct FCPMeshSim;

DECLARE_LOG_CATEGORY_EXTERN(LogCPRoadMesh, Log, All);

/** Built-in test layouts (sim_core/include/cp/test_layouts.h). */
UENUM(BlueprintType)
enum class ECPTestLayout : uint8
{
	Interchange UMETA(ToolTip = "Dual carriageway, bridge, four slip roads, roundabout and a T-junction"),
	Empty UMETA(ToolTip = "Nothing drawn; region pack and rules are still loaded"),
};

/**
 * LEGACY: kept only for the look-test screenshots (RoadTest level) until road render layers exist; do not extend. New code goes through the World and Render modules.
 * Owns a sim world (road graph, command stack, region pack, marking rules), builds render-ready road meshes and
 * road markings with sim_core and shows them as ProceduralMeshComponents: one per 256 m tile for surfaces (one
 * mesh section per surface class) and one per tile for markings (one section per colour id).
 *
 * All sim-facing logic is in Private/CPRoadMeshBuild.{h,cpp} (no Unreal types); this class only copies its plain
 * arrays into TArrays. Coordinates: see CPRoadMeshBuild.h (sim -> UE swap, winding flip; the single coordinate mapping).
 * The geometry is built in the editor viewport (OnConstruction, only when a sim-affecting setting changed), in
 * BeginPlay for game worlds, and from RebuildAll().
 */
UCLASS()
class CITYPAINTERLEGACY_API ACPRoadMeshActor : public AActor
{
	GENERATED_BODY()

public:
	ACPRoadMeshActor(const FObjectInitializer& ObjectInitializer);
	// Out-of-line (.cpp): UHT's generated FVTableHelper constructor would otherwise instantiate
	// ~TUniquePtr<FCPMeshSim> against the incomplete type.
	ACPRoadMeshActor(FVTableHelper& Helper);
	virtual ~ACPRoadMeshActor() override;

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Reload data, redraw the layout (this discards undo history) and rebuild every tile. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "CityPainter")
	void RebuildAll();

	/** Undo the last sim command and rebuild (all tiles; a dirty-tile rebuild is a TODO). */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "CityPainter")
	bool UndoLast();

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "CityPainter")
	bool RedoLast();

	// ---- layout and data ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter|Layout")
	ECPTestLayout Layout = ECPTestLayout::Interchange;

	/** Region pack id under data/regions/. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter|Layout")
	FString RegionId = TEXT("uk");

	/**
	 * Absolute path of the repo's data/ directory (holds lane_types.json and regions/). Empty = resolve from the
	 * plugin folder (four levels up, same as CityPainterSim.Build.cs), then from the project folder.
	 * TODO(packaging): data/ must be staged with the build (UFS / non-UFS directories to stage).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter|Layout")
	FDirectoryPath DataDirOverride;

	// ---- meshing ----

	/** Tile (chunk) size in metres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter|Mesh", meta = (ClampMin = "32.0"))
	float TileSizeMetres = 256.0f;

	/** Chord-error tolerance of the tessellation in metres (0.02 near, 0.1 mid, 0.5 far). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter|Mesh", meta = (ClampMin = "0.002"))
	float ChordToleranceMetres = 0.02f;

	// ---- markings ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter|Markings")
	bool bGenerateMarkings = true;

	/** Lift of the marking polygons above the road surface, in millimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter|Markings", meta = (ClampMin = "0.0"))
	float MarkingLiftMillimetres = 3.0f;

	// ---- materials ----

	/** Surface class (asphalt, paving, kerb, grass, island) -> material. Unset classes fall back to /Game/Materials/Surfaces/<slot>/MI_<slot>, then the engine default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter|Materials")
	TMap<FName, TObjectPtr<UMaterialInterface>> SurfaceMaterials;

	/** Marking colour id (white, yellow) -> material. Unset: /Game/Materials/Markings/MI_marking_<id>, then a generated tinted engine material. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter|Materials")
	TMap<FName, TObjectPtr<UMaterialInterface>> MarkingMaterials;

	// ---- debug ----

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter|Debug")
	bool bDrawConnectorPaths = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter|Debug")
	bool bDrawTrimLines = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter|Debug")
	bool bDrawLaneCentrelines = false;

	/** Outline degraded junctions (the junction builder could not build clean geometry) in red. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "CityPainter|Debug")
	bool bShowDegradedJunctions = true;

private:
	// pimpl so sim_core headers stay out of this header (and out of the actor .cpp).
	TUniquePtr<FCPMeshSim> Sim;

	UPROPERTY(Transient)
	TObjectPtr<USceneComponent> SceneRoot;

	/** Tile mesh components created at runtime; transient, destroyed and recreated on every rebuild. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UProceduralMeshComponent>> TileComponents;

	/** Fallback materials created by this actor (kept alive for GC). */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInterface>> GeneratedMaterials;

	/** Resolved-by-name cache for fallbacks (so each class logs its material once). */
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UMaterialInterface>> ResolvedSurfaceMaterials;
	UPROPERTY(Transient)
	TMap<FName, TObjectPtr<UMaterialInterface>> ResolvedMarkingMaterials;

	bool bBuilt = false;
	uint32 LastSimHash = 0;
	uint32 LastRenderHash = 0;

	void EnsureBuilt();
	bool ResolveDataDir(FString& OutDir);
	uint32 ComputeSimHash() const;
	uint32 ComputeRenderHash() const;
	/** Build FSettings, run BuildRender on the existing sim world, upload. */
	void RefreshFromSim();
	void DestroyTileComponents();
	void ApplyMaterials();
	void DrawDebug();
	UMaterialInterface* ResolveSurfaceMaterial(FName SurfaceClass);
	UMaterialInterface* ResolveMarkingMaterial(FName ColourId, const float Rgb[3]);
};
