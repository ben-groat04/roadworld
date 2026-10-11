#pragma once

#include "CoreMinimal.h"

#include "CPPlaytestTypes.h"

/**
 * Appends the milestone 2 road scenarios to the playtest: a straight 2-point road, an S-curve of 4 points, a T onto the
 * first road, undo three times, redo twice, then the right-click versus right-drag distinction. Everything goes through
 * real mouse and key events; at each step the graph's node and edge counts are checked and logged and a screenshot is taken.
 */
void AppendCPPlaytestRoadScenarios(TArray<FCPPlaytestStep>& Out);
