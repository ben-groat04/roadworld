#pragma once

// The private definition of the opaque FCPCommand (see Public/CPCommand.h).

#include "CPCommand.h"
#include "CPSimCore.h"

struct FCPCommand
{
	cp::RoadCommandPtr Command;
	std::string Name;
};
