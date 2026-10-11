#pragma once

// FCPGroundLayer: draws the terrain of the sim world as a grid of mesh tiles (one section each, ground material).
// It asks the sim facade for each tile's mesh and hands it to ICPTileMesh. It must not know about roads, input, the
// camera, or how tile meshes are stored.

#include "ICPRenderLayer.h"
#include "ICPTileMesh.h"

class CITYPAINTERRENDER_API FCPGroundLayer : public ICPRenderLayer
{
public:
	FCPGroundLayer() = default;
	FCPGroundLayer(const FCPGroundLayer&) = delete;
	FCPGroundLayer& operator=(const FCPGroundLayer&) = delete;
	static FName Name() { return FName(TEXT("Ground")); }
	virtual FName LayerName() const override { return Name(); }
	virtual void Build(const FCPRenderContext& Context) override;
	virtual void Teardown() override;

private:
	TArray<TUniquePtr<ICPTileMesh>> Tiles;
};
