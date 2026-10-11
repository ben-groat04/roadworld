#pragma once

// FCPGroundSkirtLayer: extends the terrain beyond the edge of the sim world so the world does not end in a dark void.
// The ground is continued outward by UCPRenderSettings::SkirtDistanceM at the height of the nearest edge sample (a
// clamp-to-edge extension, so it joins the terrain without a step). It reads the edge heights from the sim facade and
// hands one mesh to ICPTileMesh. It is purely visual (picking and the camera limits ignore it) and must not know about
// the ground layer's tiles.

#include "ICPRenderLayer.h"
#include "ICPTileMesh.h"

class CITYPAINTERRENDER_API FCPGroundSkirtLayer : public ICPRenderLayer
{
public:
	FCPGroundSkirtLayer() = default;
	FCPGroundSkirtLayer(const FCPGroundSkirtLayer&) = delete;
	FCPGroundSkirtLayer& operator=(const FCPGroundSkirtLayer&) = delete;
	static FName Name() { return FName(TEXT("GroundSkirt")); }
	virtual FName LayerName() const override { return Name(); }
	virtual void Build(const FCPRenderContext& Context) override;
	virtual void Teardown() override;

private:
	TUniquePtr<ICPTileMesh> Mesh;
};
