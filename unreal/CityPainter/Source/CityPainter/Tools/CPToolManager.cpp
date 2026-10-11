#include "Tools/CPToolManager.h"

#include "Tools/CPRoadDrawTool.h"
#include "Tools/CPToolActions.h"
#include "Tools/CPToolHudFormat.h"
#include "Tools/CPToolHudWidget.h"

#include "CPOverlayComponent.h"
#include "CPRenderSubsystem.h"
#include "CPSimWorld.h"
#include "CPWorldSubsystem.h"
#include "CityPainterLog.h"

#include "Blueprint/UserWidget.h"
#include "Camera/PlayerCameraManager.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

namespace
{
constexpr double StatusSeconds = 3.0;
}

UCPToolManager* UCPToolManager::Create(APlayerController* Controller, UEnhancedInputComponent* Input)
{
	UCPToolManager* M = NewObject<UCPToolManager>(Controller);
	M->Controller = Controller;
	M->Actions = NewObject<UCPToolActions>(M);
	M->Actions->Build();
	M->RoadDraw = NewObject<UCPRoadDrawTool>(M);
	M->BindInput(Input);
	return M;
}

void UCPToolManager::BindInput(UEnhancedInputComponent* Input)
{
	// Buttons: pressed and released (a click is decided on release, see FCPClickGesture).
	Input->BindAction(Actions->Primary, ETriggerEvent::Started, this, &UCPToolManager::OnPrimary, true);
	Input->BindAction(Actions->Primary, ETriggerEvent::Completed, this, &UCPToolManager::OnPrimary, false);
	Input->BindAction(Actions->Secondary, ETriggerEvent::Started, this, &UCPToolManager::OnSecondary, true);
	Input->BindAction(Actions->Secondary, ETriggerEvent::Completed, this, &UCPToolManager::OnSecondary, false);
	// Keys: one event per press.
	for (int32 I = 0; I < Actions->KeyActions.Num(); ++I)
	{
		Input->BindAction(Actions->KeyActions[I], ETriggerEvent::Started, this, &UCPToolManager::OnKeyAction, Actions->Keys[I]);
	}
}

void UCPToolManager::Begin()
{
	if (bBegun || !Controller)
	{
		return;
	}
	bBegun = true;
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer()))
	{
		Subsystem->AddMappingContext(Actions->Context, 1);
	}
	else
	{
		UE_LOG(LogCPGame, Error, TEXT("No Enhanced Input subsystem: the road tool has no input"));
	}
	Hud = CreateWidget<UCPToolHudWidget>(Controller, UCPToolHudWidget::StaticClass());
	if (Hud)
	{
		Hud->AddToViewport(1);
	}
	SetActiveTool(RoadDraw);
}

void UCPToolManager::SetActiveTool(UCPTool* Tool)
{
	const FCPToolContext Context = MakeContext();
	if (ActiveTool)
	{
		ActiveTool->OnDeactivated(Context);
	}
	ActiveTool = Tool;
	LastSignature = FHoverSignature();
	if (ActiveTool)
	{
		ActiveTool->OnActivated(Context);
	}
}

bool UCPToolManager::PointerPx(FVector2D& Out) const
{
	double X = 0, Y = 0;
	if (!Controller || !Controller->GetMousePosition(X, Y))
	{
		return false;
	}
	Out = FVector2D(X, Y);
	return true;
}

FCPToolContext UCPToolManager::MakeContext() const
{
	FCPToolContext C;
	UWorld* World = Controller ? Controller->GetWorld() : nullptr;
	if (!World)
	{
		return C;
	}
	C.World = World->GetSubsystem<UCPWorldSubsystem>();
	C.TimeSeconds = World->GetTimeSeconds();
	C.bSnapOff = Controller->IsInputKeyDown(EKeys::LeftShift) || Controller->IsInputKeyDown(EKeys::RightShift);
	FVector2D Pointer;
	FVector Origin, Direction, Origin2, Direction2;
	if (PointerPx(Pointer) && Controller->DeprojectScreenPositionToWorld(static_cast<float>(Pointer.X), static_cast<float>(Pointer.Y), Origin, Direction)
		&& Controller->DeprojectScreenPositionToWorld(static_cast<float>(Pointer.X + 1.0), static_cast<float>(Pointer.Y), Origin2, Direction2))
	{
		// Ground metres per screen pixel per metre of distance = the angle one pixel spans (small angle), the same scale
		// the sim multiplies by the hit distance for its snap radii.
		const double Angle = FMath::Acos(FMath::Clamp(FVector::DotProduct(Direction.GetSafeNormal(), Direction2.GetSafeNormal()), -1.0, 1.0));
		if (Angle > 0.0)
		{
			C.bHasRay = true;
			C.RayOriginCm = Origin;
			C.RayDirectionUE = Direction;
			C.MetresPerPixelPerMetre = Angle;
		}
	}
	return C;
}

void UCPToolManager::Tick(float DeltaTime)
{
	if (!bBegun || !ActiveTool)
	{
		return;
	}
	const FCPToolContext Context = MakeContext();
	if (!Context.World)
	{
		return;
	}
	if (!StatusText.IsEmpty() && Context.TimeSeconds > StatusExpires)
	{
		StatusText.Reset();
	}

	FHoverSignature Signature;
	PointerPx(Signature.Pointer);
	if (Controller->PlayerCameraManager)
	{
		Signature.CameraLocation = Controller->PlayerCameraManager->GetCameraLocation();
		Signature.CameraRotation = Controller->PlayerCameraManager->GetCameraRotation();
	}
	Signature.ToolRevision = ActiveTool->GetRevision();
	Signature.WorldVersion = Context.World->GetSim() ? Context.World->GetSim()->WorldVersion() : 0;
	Signature.bSnapOff = Context.bSnapOff;
	// While a timed message is on screen, re-evaluate a few times a second so it disappears on time.
	const bool bTimed = !ActiveTool->GetFeedback().RefusalText.IsEmpty() || !StatusText.IsEmpty();
	Signature.ToolRevision += bTimed ? static_cast<int32>(Context.TimeSeconds * 4.0) * 1000003 : 0;
	if (Signature == LastSignature)
	{
		return;
	}
	LastSignature = Signature;

	ActiveTool->OnHover(Context);
	const FCPToolFeedback& Feedback = ActiveTool->GetFeedback();

	if (const UCPRenderSubsystem* Render = Context.World->GetWorld()->GetSubsystem<UCPRenderSubsystem>())
	{
		if (UCPOverlayComponent* Overlay = Render->GetOverlay())
		{
			if (Feedback.bHasPreview)
			{
				Overlay->SetPreview(Feedback.Preview);
			}
			else
			{
				Overlay->Clear();
			}
		}
	}
	if (Hud)
	{
		TArray<FCPHudLine> Lines = CPToolHudFormat::BuildLines(Feedback);
		if (!StatusText.IsEmpty())
		{
			Lines.Add({StatusText, FLinearColor::White});
		}
		Hud->SetLines(Lines);
		Hud->SetCue(CPToolHudFormat::BuildCue(Feedback));
	}
}

void UCPToolManager::OnPrimary(bool bPressed)
{
	FVector2D P;
	if (!PointerPx(P))
	{
		return;
	}
	if (bPressed)
	{
		PrimaryGesture.Press(P);
	}
	else if (PrimaryGesture.Release(P) && ActiveTool)
	{
		ActiveTool->OnClick(MakeContext());
	}
}

void UCPToolManager::OnSecondary(bool bPressed)
{
	FVector2D P;
	if (!PointerPx(P))
	{
		return;
	}
	if (bPressed)
	{
		SecondaryGesture.Press(P);
	}
	else if (SecondaryGesture.Release(P) && ActiveTool)
	{
		ActiveTool->OnSecondaryClick(MakeContext());   // a drag already rotated the camera and is not a click
	}
}

void UCPToolManager::OnKeyAction(FKey Key)
{
	const bool bCtrl = Controller->IsInputKeyDown(EKeys::LeftControl) || Controller->IsInputKeyDown(EKeys::RightControl);
	const bool bShift = Controller->IsInputKeyDown(EKeys::LeftShift) || Controller->IsInputKeyDown(EKeys::RightShift);
	if (Key == EKeys::Z)
	{
		if (bCtrl)
		{
			bShift ? DoRedo() : DoUndo();   // Ctrl+Shift+Z redoes
		}
		return;
	}
	if (Key == EKeys::Y)
	{
		if (bCtrl)
		{
			DoRedo();
		}
		return;
	}
	if (bCtrl || !ActiveTool)
	{
		return;   // other Ctrl combinations are not ours
	}
	if (Key == EKeys::Escape)
	{
		ActiveTool->OnCancel(MakeContext());
	}
	else
	{
		ActiveTool->OnKey(Key, MakeContext());
	}
}

void UCPToolManager::DoUndo()
{
	const FCPToolContext Context = MakeContext();
	if (!Context.World)
	{
		return;
	}
	const FCPEditResult R = Context.World->Undo();
	ShowStatus(R.bOk ? TEXT("Undo") : TEXT("Nothing to undo"));
}

void UCPToolManager::DoRedo()
{
	const FCPToolContext Context = MakeContext();
	if (!Context.World)
	{
		return;
	}
	const FCPEditResult R = Context.World->Redo();
	ShowStatus(R.bOk ? TEXT("Redo") : TEXT("Nothing to redo"));
}

void UCPToolManager::ShowStatus(const FString& Text)
{
	StatusText = Text;
	StatusExpires = (Controller && Controller->GetWorld() ? Controller->GetWorld()->GetTimeSeconds() : 0.0) + StatusSeconds;
	LastSignature = FHoverSignature();   // redraw the HUD now
}

FString UCPToolManager::GetHudText() const
{
	return Hud ? Hud->GetShownText() : FString();
}
