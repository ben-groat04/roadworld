#pragma once

// UCPRenderSubsystem: per-world owner of the render layers. Once the sim world exists it spawns one transient root
// actor, creates every registered layer (FCPRenderLayerRegistry) and asks each to build. Works in the editor world,
// play-in-editor and games alike. It must not know what any layer draws, nor input or UI.

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

#include "ICPRenderLayer.h"

#include "CPRenderSubsystem.generated.h"

class AActor;

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

private:
	void BuildLayers();

	TArray<TUniquePtr<ICPRenderLayer>> Layers;
	TWeakObjectPtr<AActor> Root;
	bool bBuilt = false;
};
