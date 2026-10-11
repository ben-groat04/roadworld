#pragma once

// ICPTileMesh: the one contract for "a chunk of static triangle mesh in the world". Layers use only this; the
// implementation (today a ProceduralMeshComponent, see Private/CPProcTileMesh.cpp, the only file that includes it)
// can be swapped for another mesh technology without touching any layer. It must not know what the mesh represents
// (ground, road, markings) and layers must not know how it is stored.

#include "CoreMinimal.h"

#include "CPMeshData.h"

class AActor;
class UMaterialInterface;

class CITYPAINTERRENDER_API ICPTileMesh
{
public:
	virtual ~ICPTileMesh() = default;

	/** Creates or replaces one section. The data is positioned by Data.OriginUeCm. */
	virtual void SetSection(int32 SectionIndex, const FCPMeshSection& Data, UMaterialInterface* Material) = 0;
	virtual void Clear() = 0;

	/** A tile mesh owned by `Owner` (the component lives and dies with that actor). */
	static TUniquePtr<ICPTileMesh> Create(AActor& Owner, FName DebugName);
};
