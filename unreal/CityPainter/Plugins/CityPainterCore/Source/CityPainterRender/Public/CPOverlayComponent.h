#pragma once

// UCPOverlayComponent: draws the road tool's feedback in the world, rebuilt on every hover (it is not tiled): the ghost
// ribbon (white; red only when the preview has an error, amber advisories do not recolour it), the snap ring in its
// three colours, the three kinds of dashed guide, the dots of the placed points and the finish marker. Sizes of the
// ring, dots and finish marker are constant on screen: they are scaled by the metres-per-pixel the preview carries.
// The ribbon is a mesh (through ICPTileMesh); the rest are batched lines and points. It takes a preview from the World
// module and draws it; it must not know about input, the tool state machine or the sim.

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"

#include "CPToolBridge.h"
#include "ICPTileMesh.h"

#include "CPOverlayComponent.generated.h"

class ULineBatchComponent;
class UMaterialInstanceDynamic;

UCLASS()
class CITYPAINTERRENDER_API UCPOverlayComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCPOverlayComponent();

	/** Redraws everything for this preview (and hides everything when it has no pick). */
	void SetPreview(const FCPUEToolPreview& Preview);
	/** Hides everything. */
	void Clear();

	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	void EnsureObjects();
	void DrawRibbon(const FCPUEToolPreview& Preview);
	void DrawMarks(const FCPUEToolPreview& Preview);

	UPROPERTY(Transient) TObjectPtr<ULineBatchComponent> Lines;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GhostNormal;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GhostError;
	TUniquePtr<ICPTileMesh> Ribbon;
	bool bObjectsReady = false;
};
