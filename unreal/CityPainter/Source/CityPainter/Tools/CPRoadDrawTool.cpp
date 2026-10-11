#include "Tools/CPRoadDrawTool.h"

#include "CPSettings.h"
#include "CPToolSession.h"
#include "CPWorldSubsystem.h"
#include "CityPainterLog.h"

namespace
{
constexpr double RefusalSeconds = 5.0;      // how long a refusal stays on screen
constexpr double HeightStepM = 1.0;         // PgUp / PgDn
constexpr double HeightStepShiftM = 5.0;    // with Shift
}  // namespace

FCPToolSession* UCPRoadDrawTool::Session(const FCPToolContext& Context) const
{
	return Context.World ? Context.World->GetToolSession() : nullptr;
}

void UCPRoadDrawTool::OnActivated(const FCPToolContext& Context)
{
	// Start on the first palette type that exists in the loaded region pack.
	if (FCPToolSession* S = Session(Context))
	{
		const TArray<FString>& Palette = GetDefault<UCPSettings>()->RoadTypePalette;
		const std::vector<FCPRoadTypeInfo> Types = Context.World->GetSim()->RoadTypes();
		for (const FString& Id : Palette)
		{
			const std::string Std = CPToolBridge::ToStd(Id);
			for (const FCPRoadTypeInfo& T : Types)
			{
				if (T.Id == Std && !T.bRoundaboutRing)
				{
					S->SetRoadType(Std);
					Touch();
					return;
				}
			}
		}
		UE_LOG(LogCPGame, Warning, TEXT("None of the road types in UCPSettings::RoadTypePalette exists in the region pack; the road tool has no type until a number key selects one"));
	}
}

void UCPRoadDrawTool::OnDeactivated(const FCPToolContext& Context)
{
	if (FCPToolSession* S = Session(Context))
	{
		S->Cancel();
	}
	Feedback = FCPToolFeedback();
	Touch();
}

void UCPRoadDrawTool::OnHover(const FCPToolContext& Context)
{
	if (Context.TimeSeconds > RefusalExpires)
	{
		Feedback.RefusalText.Reset();
	}
	FCPToolSession* S = Session(Context);
	if (!S)
	{
		Feedback.bHasPreview = false;
		Feedback.RefusalText = TEXT("Roads are unavailable: the region pack did not load (see the log)");
		return;
	}
	if (!Context.bHasRay)
	{
		return;   // the pointer left the viewport: keep the last preview
	}
	FCPToolModifiers Mods;
	Mods.bSnapOff = Context.bSnapOff;
	FCPUEToolPreview Preview = CPToolBridge::PreviewToUE(S->Hover(CPToolBridge::PointerFromUE(Context.RayOriginCm, Context.RayDirectionUE, Context.MetresPerPixelPerMetre), Mods));
	// Pixel size at the cursor, for the overlay's constant-size marks (the same scale the sim used for its snap radii).
	Preview.MetresPerPixel = Context.MetresPerPixelPerMetre * FVector::Dist(Context.RayOriginCm, Preview.CursorCm) / 100.0;
	Feedback.bHasPreview = Preview.bHasPick;
	Feedback.bSnapOff = Context.bSnapOff;
	Preview.bRefused = !Feedback.RefusalText.IsEmpty();
	Feedback.Preview = MoveTemp(Preview);
}

void UCPRoadDrawTool::OnClick(const FCPToolContext& Context)
{
	FCPToolSession* S = Session(Context);
	if (!S || !Context.bHasRay)
	{
		return;
	}
	ClearRefusalOnAction();
	FCPToolModifiers Mods;
	Mods.bSnapOff = Context.bSnapOff;
	FCPToolClickResult R = S->Click(CPToolBridge::PointerFromUE(Context.RayOriginCm, Context.RayDirectionUE, Context.MetresPerPixelPerMetre), Mods);
	Touch();
	if (!R.bAdded && !R.bFinished)
	{
		if (!R.Errors.empty())
		{
			Refuse(CPToolBridge::ToFString(R.Errors.front().Text), Context);   // a red error was current: the click is refused
		}
		else if (!R.Finish.bOk && !R.Finish.Error.empty())
		{
			Refuse(CPToolBridge::ToFString(R.Finish.Error), Context);
		}
	}
	if (R.bFinished && R.Finish.Command)
	{
		const FCPEditResult Res = Context.World->Submit(MoveTemp(R.Finish.Command));
		if (!Res.bOk)
		{
			Refuse(CPToolBridge::ToFString(Res.Error), Context);
		}
	}
}

void UCPRoadDrawTool::OnSecondaryClick(const FCPToolContext& Context)
{
	FCPToolSession* S = Session(Context);
	if (!S || !S->IsActive())
	{
		return;
	}
	ClearRefusalOnAction();
	if (S->PointCount() < 2)
	{
		S->Cancel();   // right-click with fewer than two points cancels
		Touch();
		return;
	}
	FinishStroke(Context);
}

void UCPRoadDrawTool::OnCancel(const FCPToolContext& Context)
{
	if (FCPToolSession* S = Session(Context))
	{
		if (S->IsActive())
		{
			S->Cancel();
			ClearRefusalOnAction();
			Touch();
		}
	}
}

void UCPRoadDrawTool::OnKey(const FKey& Key, const FCPToolContext& Context)
{
	FCPToolSession* S = Session(Context);
	if (!S)
	{
		return;
	}
	if (Key == EKeys::Enter)
	{
		if (S->IsActive())
		{
			ClearRefusalOnAction();
			FinishStroke(Context);
		}
	}
	else if (Key == EKeys::BackSpace)
	{
		ClearRefusalOnAction();
		S->UndoPoint();
		Touch();
	}
	else if (Key == EKeys::One || Key == EKeys::Two || Key == EKeys::Three || Key == EKeys::Four)
	{
		SelectRoadTypeSlot(Key == EKeys::One ? 0 : (Key == EKeys::Two ? 1 : (Key == EKeys::Three ? 2 : 3)), Context);
	}
	else if (Key == EKeys::O)
	{
		S->SetOneWay(!S->OneWay());
		Touch();
	}
	else if (Key == EKeys::PageUp || Key == EKeys::RightBracket)
	{
		S->AdjustHeight(Context.bSnapOff ? HeightStepShiftM : HeightStepM);
		Touch();
	}
	else if (Key == EKeys::PageDown || Key == EKeys::LeftBracket)
	{
		S->AdjustHeight(-(Context.bSnapOff ? HeightStepShiftM : HeightStepM));
		Touch();
	}
}

void UCPRoadDrawTool::FinishStroke(const FCPToolContext& Context)
{
	FCPToolSession* S = Session(Context);
	if (!S)
	{
		return;
	}
	FCPToolFinishResult F = S->Finish();
	Touch();
	if (!F.bOk)
	{
		Refuse(CPToolBridge::ToFString(F.Error), Context);   // the stroke stays open so the player can fix it
		return;
	}
	const FCPEditResult Res = Context.World->Submit(MoveTemp(F.Command));
	if (!Res.bOk)
	{
		Refuse(CPToolBridge::ToFString(Res.Error), Context);
	}
}

void UCPRoadDrawTool::SelectRoadTypeSlot(int32 Slot, const FCPToolContext& Context)
{
	FCPToolSession* S = Session(Context);
	const TArray<FString>& Palette = GetDefault<UCPSettings>()->RoadTypePalette;
	if (!S || !Palette.IsValidIndex(Slot))
	{
		Refuse(FString::Printf(TEXT("No road type is assigned to key %d (UCPSettings::RoadTypePalette)"), Slot + 1), Context);
		return;
	}
	const std::string Id = CPToolBridge::ToStd(Palette[Slot]);
	for (const FCPRoadTypeInfo& T : Context.World->GetSim()->RoadTypes())
	{
		if (T.Id == Id && !T.bRoundaboutRing)
		{
			S->SetRoadType(Id);
			Touch();
			return;
		}
	}
	Refuse(FString::Printf(TEXT("Road type '%s' (key %d) is not in the region pack"), *Palette[Slot], Slot + 1), Context);
}

void UCPRoadDrawTool::Refuse(const FString& Text, const FCPToolContext& Context)
{
	Feedback.RefusalText = Text;
	RefusalExpires = Context.TimeSeconds + RefusalSeconds;
	Touch();
	UE_LOG(LogCPGame, Display, TEXT("Road tool: %s"), *Text);
}

void UCPRoadDrawTool::ClearRefusalOnAction()
{
	Feedback.RefusalText.Reset();
}
