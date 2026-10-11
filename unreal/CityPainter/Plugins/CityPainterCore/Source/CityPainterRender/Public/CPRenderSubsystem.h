#pragma once

// UCPRenderSubsystem: per-world owner of the render layers. Once the sim world exists it spawns one transient root
// actor, creates every registered layer (FCPRenderLayerRegistry) and asks each to build, then forwards every edit's changed
// tiles to the layers and owns the tool overlay component. Works in the editor world,
// play-in-editor and games alike. It must not know what any layer draws, nor input or UI.

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "ICPRenderLayer.h"

#include "CPRenderSubsystem.generated.h"

class AActor;
class UCPOverlayComponent;
class UCPWorldSubsystem;
struct FCPChangeSet;

UCLASS()
class CITYPAINTERRENDER_API UCPRenderSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	UCPRenderSubsystem();
	~UCPRenderSubsystem();

	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickableInEditor() const override { return true; }

	bool AreLayersBuilt() const { return bBuilt; }

	/** The road tool feedback overlay (created with the layers); null before they are built. */
	UCPOverlayComponent* GetOverlay() const;

private:
	void BuildLayers();
	/** Forwards the tiles an edit changed to every layer (before the layers exist the later catch-up snapshot covers them). */
	void HandleTilesChanged(const FCPChangeSet& Changes);
	/** Logs actor and mesh-component counts of this world (for leak checks across play-in-editor sessions). */
	void LogCounts(const TCHAR* When) const;

	TArray<TUniquePtr<ICPRenderLayer>> Layers;
	TWeakObjectPtr<AActor> Root;
	UPROPERTY(Transient) TObjectPtr<UCPOverlayComponent> Overlay;
	TWeakObjectPtr<UCPWorldSubsystem> WorldSubsystem;
	FDelegateHandle TilesChangedHandle;
	bool bBuilt = false;
};
