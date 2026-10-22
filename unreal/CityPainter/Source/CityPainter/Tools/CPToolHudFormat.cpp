#include "Tools/CPToolHudFormat.h"

#include <cmath>

namespace
{
// Theme tokens of the prototype (docs/unreal-ui-parity.md section 0), sRGB hex.
FLinearColor Hex(uint8 R, uint8 G, uint8 B)
{
	return FLinearColor::FromSRGBColor(FColor(R, G, B));
}
FLinearColor TextColour() { return Hex(0xe6, 0xed, 0xf3); }
FLinearColor ErrorColour() { return Hex(0xff, 0x7b, 0x72); }
FLinearColor WarningColour() { return Hex(0xf5, 0xa6, 0x23); }

const TCHAR* SnapWord(ECPSnapKind Kind)
{
	switch (Kind)
	{
	case ECPSnapKind::Angle: return TEXT("angle");
	case ECPSnapKind::Guide: return TEXT("guide");
	case ECPSnapKind::End: return TEXT("end");
	case ECPSnapKind::Node: return TEXT("node");
	case ECPSnapKind::Road: return TEXT("road");
	default: return TEXT("free");
	}
}

const TCHAR* Dot() { return TEXT(" · "); }
}  // namespace

namespace CPToolHudFormat
{
FString FormatRadius(double RadiusM)
{
	if (!std::isfinite(RadiusM) || RadiusM > 100000.0)
	{
		const TCHAR InfinityGlyph = 0x221E;   // the prototype prints the infinity sign
		return FString(1, &InfinityGlyph);
	}
	return FString::Printf(TEXT("%.0f"), RadiusM);
}

TArray<FCPHudLine> BuildLines(const FCPToolFeedback& F)
{
	TArray<FCPHudLine> Lines;
	const FCPUEToolPreview& P = F.Preview;

	// 1. Header: mode, road type, one-way / single carriageway, snap off.
	FString Header = TEXT("DRAW");
	if (!P.RoadTypeName.IsEmpty())
	{
		Header += Dot() + P.RoadTypeName;
	}
	if (P.bOneWay)
	{
		Header += Dot() + (P.bRoadTypeDual ? FString(TEXT("single carriageway")) : FString(TEXT("one-way")));
	}
	if (F.bSnapOff)
	{
		Header += Dot() + FString(TEXT("snap off"));
	}
	Lines.Add({Header, F.bSnapOff ? WarningColour() : TextColour()});

	if (F.bHasPreview && P.bHasPick)
	{
		// 2. Cursor height (with the PgUp/PgDn offset) and the snap kind.
		FString Cursor = FString::Printf(TEXT("cursor z %.1f m"), P.Readouts.HeightM);
		if (FMath::Abs(P.HeightOffsetM) > 1e-9)
		{
			Cursor += FString::Printf(TEXT(" (%+.0f m)"), P.HeightOffsetM);
		}
		Cursor += Dot() + FString::Printf(TEXT("snap: %s"), SnapWord(P.Snap));
		Lines.Add({Cursor, TextColour()});

		// 3-4. Readouts of the stroke; only while the sim has a preview road (not when the cursor sits on the last point).
		if (P.Ribbons.Num() > 0 && !P.bWouldFinish)
		{
			const FCPPreviewReadouts& R = P.Readouts;
			Lines.Add({FString::Printf(TEXT("span %.1f m%sbearing %.0f°%sR at cursor %s m%smin R %s m"), R.SpanLengthM, Dot(), R.HeadingDeg, Dot(), *FormatRadius(R.EndRadiusM), Dot(), *FormatRadius(R.MinRadiusM)), TextColour()});
			Lines.Add({FString::Printf(TEXT("grade %+.1f%%%smax %.1f%%%stotal %.0f m"), R.GradePct, Dot(), R.MaxGradePct, Dot(), R.LengthM), TextColour()});
		}
	}

	// 5. Errors (red), 6. advisories (amber), then a refusal that outlives the hover that caused it.
	for (const FCPUEIssue& E : P.Errors)
	{
		Lines.Add({TEXT("X ") + E.Text, ErrorColour()});
	}
	for (const FCPUEIssue& A : P.Advisories)
	{
		Lines.Add({TEXT("! ") + A.Text, WarningColour()});
	}
	if (!F.RefusalText.IsEmpty())
	{
		Lines.Add({TEXT("X refused: ") + F.RefusalText, ErrorColour()});
	}
	return Lines;
}

FString BuildCue(const FCPToolFeedback& F)
{
	const FCPUEToolPreview& P = F.Preview;
	if (!P.bStrokeActive)
	{
		return TEXT("Click to start a road   |   1-4 road type   O one-way   PgUp/PgDn height   Ctrl+Z / Ctrl+Y undo / redo");
	}
	if (P.bWouldFinish)
	{
		return TEXT("Click again here to finish the road   |   or press Enter / right-click");
	}
	if (P.ClickedCm.Num() < 2)
	{
		return TEXT("Click to add points   |   Backspace removes the last point   Esc cancels");
	}
	return TEXT("Press Enter or right-click to finish   |   Backspace removes the last point   Esc cancels");
}
}  // namespace CPToolHudFormat
