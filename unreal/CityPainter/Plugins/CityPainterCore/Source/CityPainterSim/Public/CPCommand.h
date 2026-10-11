#pragma once

// FCPCommandHandle: an opaque, move-only handle to one player edit (a cp::Command) that the tool session produced and
// the sim world will execute. Upper modules can hold it and pass it on but cannot look inside, so no sim_core type
// leaks out of the Sim module. It must not know how the edit is produced or applied.

#include "CPSimApi.h"

#include <memory>
#include <string>

struct FCPCommand;  // defined in Private/CPCommandImpl.h

struct CITYPAINTERSIM_API FCPCommandDeleter
{
	void operator()(FCPCommand* Command) const;
};

using FCPCommandHandle = std::unique_ptr<FCPCommand, FCPCommandDeleter>;

/** The command's display name ("draw road", ...), empty for a null handle. */
CITYPAINTERSIM_API std::string CPCommandName(const FCPCommandHandle& Command);
