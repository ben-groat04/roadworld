#pragma once

#include "CoreMinimal.h"

#include "CPPlaytestTypes.h"

/** Appends the whole scripted playtest (every control, in order) to Out. Add new milestones' checks inside it. */
void BuildCPPlaytestScript(TArray<FCPPlaytestStep>& Out);
