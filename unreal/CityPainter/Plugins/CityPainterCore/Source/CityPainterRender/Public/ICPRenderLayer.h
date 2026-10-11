#pragma once

// ICPRenderLayer: one drawable system (ground today; roads, markings, structures, instanced kit, vegetation later).
// A layer reads the sim through the facade and creates its meshes through ICPTileMesh. Adding a system = one new
// layer file plus one registration line in the Render module startup; no existing layer changes. It must not know
// about other layers, input, or the UI.

#include "CoreMinimal.h"

class AActor;
class FCPSimWorld;
struct FCPChangeSet;
class UWorld;

struct FCPRenderContext
{
	UWorld* World = nullptr;
	AActor* Root = nullptr;      // owner of every component the layers create
	FCPSimWorld* Sim = nullptr;
};

class CITYPAINTERRENDER_API ICPRenderLayer
{
public:
	virtual ~ICPRenderLayer() = default;
	virtual FName LayerName() const = 0;
	/** Creates everything this layer draws for the current sim state. Game thread. */
	virtual void Build(const FCPRenderContext& Context) = 0;
	/** Drops everything the layer created (the world is going away or is rebuilt). */
	virtual void Teardown() = 0;
	/** The sim changed some tiles (an edit, undo or redo). Layers that follow edits override this; static layers ignore it. Game thread. */
	virtual void OnTilesChanged(const FCPChangeSet& Changes) {}
};

/** Where layers announce themselves; each layer's registration is a single line in the module startup. */
class CITYPAINTERRENDER_API FCPRenderLayerRegistry
{
public:
	using FFactory = TFunction<TUniquePtr<ICPRenderLayer>()>;
	static FCPRenderLayerRegistry& Get();
	void Register(FName Name, FFactory Factory);
	void Unregister(FName Name);
	TArray<TUniquePtr<ICPRenderLayer>> CreateAll() const;

private:
	TArray<TPair<FName, FFactory>> Entries;
};
