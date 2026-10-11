#pragma once

// The private state of FCPToolSession (shared with FCPSimWorld, which builds it).

#include "CPSimCore.h"
#include "CPToolSession.h"

struct FCPToolSession::FImpl
{
	FImpl(const cp::RoadGraph& Graph, const cp::Heightfield& Ground) : Session(Graph, Ground) {}

	cp::DrawSession Session;
	const cp::RegionPack* Region = nullptr;     // for road type names (owned by the sim world)
	std::vector<FCPPreviewRibbon> LastRibbons;  // the last hover's ribbons, shown again while the cursor sits on the last point
};
