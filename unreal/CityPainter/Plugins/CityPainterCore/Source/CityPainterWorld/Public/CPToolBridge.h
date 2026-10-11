#pragma once

// The tool data in Unreal's frame. The Sim module speaks sim metres; everything above the World module (render overlay,
// game tools, HUD) speaks Unreal centimetres. This is the one place where the tool's pointer and preview cross that
// border (through CPFrame.h), the same job UCPWorldSubsystem::PickGroundUE does for ground picks. It must not know how
// the data is drawn or what the player pressed.

#include "CoreMinimal.h"

#include "CPToolTypes.h"

struct FCPUEPreviewRibbon
{
	TArray<FVector> CommittedCm, HoverCm;   // centreline up to the last clicked point / on to the cursor
	double WidthM = 0;
};

struct FCPUEPreviewGuide
{
	ECPGuideKind Kind = ECPGuideKind::None;
	TArray<FVector> PolylineCm;
	bool bFaint = false;
};

struct FCPUEIssue
{
	ECPIssueCode Code = ECPIssueCode::CannotFit;
	FString Text;
};

struct FCPUEToolPreview
{
	bool bHasPick = false;
	FVector CursorCm = FVector::ZeroVector;
	ECPSnapKind Snap = ECPSnapKind::Free;
	FCPUEPreviewGuide Guide;
	TArray<FVector> ClickedCm;
	TArray<FCPUEPreviewRibbon> Ribbons;
	TArray<FCPUEIssue> Errors, Advisories;
	FCPPreviewReadouts Readouts;            // metres / percent / degrees: frame independent
	bool bWouldFinish = false;
	bool bStrokeActive = false;
	bool bOneWay = false;
	double HeightOffsetM = 0;
	FString RoadTypeName;
	bool bRoadTypeDual = false;
	double MetresPerPixel = 0;              // ground metres per screen pixel at the cursor (for constant-size overlay marks)

	bool HasErrors() const { return Errors.Num() > 0; }
};

namespace CPToolBridge
{
CITYPAINTERWORLD_API FString ToFString(const std::string& S);
CITYPAINTERWORLD_API std::string ToStd(const FString& S);
/** A pointer ray in Unreal's frame (centimetres) to the sim frame. */
CITYPAINTERWORLD_API FCPPointerInput PointerFromUE(const FVector& OriginCm, const FVector& DirectionUE, double MetresPerPixelPerMetre);
CITYPAINTERWORLD_API FCPUEToolPreview PreviewToUE(const FCPToolPreview& Preview);
CITYPAINTERWORLD_API TArray<FCPUEIssue> IssuesToUE(const std::vector<FCPPreviewIssue>& Issues);
}  // namespace CPToolBridge
