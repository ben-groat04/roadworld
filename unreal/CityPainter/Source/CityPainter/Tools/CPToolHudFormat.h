#pragma once

// Turns a tool's feedback into the HUD's lines and the bottom cue, as docs/unreal-ui-parity.md A.1 specifies (header,
// cursor line, span/bearing/radius line, grade line, errors in red, advisories in amber, a refusal that stays). Pure
// functions of plain data: no widgets, no sim, no input. It must not know how the lines are drawn.

#include "CoreMinimal.h"

#include "Tools/CPTool.h"

struct FCPHudLine
{
	FString Text;
	FLinearColor Colour = FLinearColor::White;
};

namespace CPToolHudFormat
{
/** The HUD lines for the road tool (mode header first). */
TArray<FCPHudLine> BuildLines(const FCPToolFeedback& Feedback);
/** The one-line key hint shown at the bottom while drawing; never empty. */
FString BuildCue(const FCPToolFeedback& Feedback);
/** "inf" for a straight (or anything beyond 100 km), whole metres otherwise. */
FString FormatRadius(double RadiusM);
}  // namespace CPToolHudFormat
