#include "CPToolBridge.h"

#include "CPFrame.h"

namespace
{
FVector ToUE(const CPFrame::FD3& Sim)
{
	const CPFrame::FD3 U = CPFrame::SimToUE(Sim);
	return FVector(U.X, U.Y, U.Z);
}

TArray<FVector> ToUE(const std::vector<CPFrame::FD3>& Sim)
{
	TArray<FVector> Out;
	Out.Reserve(static_cast<int32>(Sim.size()));
	for (const CPFrame::FD3& P : Sim)
	{
		Out.Add(ToUE(P));
	}
	return Out;
}
}  // namespace

namespace CPToolBridge
{
FString ToFString(const std::string& S)
{
	return FString(UTF8_TO_TCHAR(S.c_str()));
}

std::string ToStd(const FString& S)
{
	const FTCHARToUTF8 Utf8(*S);
	return std::string(Utf8.Get(), Utf8.Length());
}

FCPPointerInput PointerFromUE(const FVector& OriginCm, const FVector& DirectionUE, double MetresPerPixelPerMetre)
{
	FCPPointerInput P;
	P.OriginSim = CPFrame::UEToSim({OriginCm.X, OriginCm.Y, OriginCm.Z});
	P.DirSim = CPFrame::UEDirToSim({DirectionUE.X, DirectionUE.Y, DirectionUE.Z});
	P.MetresPerPixelPerMetre = MetresPerPixelPerMetre;
	return P;
}

TArray<FCPUEIssue> IssuesToUE(const std::vector<FCPPreviewIssue>& Issues)
{
	TArray<FCPUEIssue> Out;
	for (const FCPPreviewIssue& I : Issues)
	{
		Out.Add({I.Code, ToFString(I.Text)});
	}
	return Out;
}

FCPUEToolPreview PreviewToUE(const FCPToolPreview& In)
{
	FCPUEToolPreview Out;
	Out.bHasPick = In.bHasPick;
	Out.bStrokeActive = In.bStrokeActive;
	Out.bOneWay = In.bOneWay;
	Out.HeightOffsetM = In.HeightOffsetM;
	Out.RoadTypeName = ToFString(In.RoadTypeName);
	Out.bRoadTypeDual = In.bRoadTypeDual;
	if (!In.bHasPick)
	{
		return Out;
	}
	Out.CursorCm = ToUE(In.Cursor);
	Out.Snap = In.Snap;
	Out.Guide.Kind = In.Guide.Kind;
	Out.Guide.PolylineCm = ToUE(In.Guide.Polyline);
	Out.Guide.bFaint = In.Guide.bFaint;
	Out.ClickedCm = ToUE(In.Clicked);
	for (const FCPPreviewRibbon& R : In.Ribbons)
	{
		FCPUEPreviewRibbon& Rib = Out.Ribbons.AddDefaulted_GetRef();
		Rib.CommittedCm = ToUE(R.Committed);
		Rib.HoverCm = ToUE(R.Hover);
		Rib.WidthM = R.WidthM;
	}
	Out.Errors = IssuesToUE(In.Errors);
	Out.Advisories = IssuesToUE(In.Advisories);
	Out.Readouts = In.Readouts;
	Out.bWouldFinish = In.bWouldFinish;
	return Out;
}
}  // namespace CPToolBridge
