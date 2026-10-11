#include "CPPlayerController.h"

#include "CPCameraPawn.h"
#include "CPInputConfig.h"
#include "CityPainterLog.h"

#include "Blueprint/UserWidget.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "InputActionValue.h"

namespace
{
constexpr double TwoPi = 6.283185307179586;
}

ACPPlayerController::ACPPlayerController()
{
	bShowMouseCursor = true;
	DefaultMouseCursor = EMouseCursor::Default;
}

ACPCameraPawn* ACPPlayerController::CameraPawn() const
{
	return Cast<ACPCameraPawn>(GetPawn());
}

void ACPPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);

	if (InputConfig)
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(InputConfig->Context, 0);
		}
	}
	Hud = CreateWidget<UCPHudWidget>(this, UCPHudWidget::StaticClass());
	if (Hud)
	{
		Hud->AddToViewport();
	}
}

void ACPPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	InputConfig = NewObject<UCPInputConfig>(this);
	InputConfig->Build();
	UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent);
	if (!EIC)
	{
		UE_LOG(LogCPGame, Error, TEXT("Input component is not an EnhancedInputComponent; check DefaultInputComponentClass in DefaultInput.ini"));
		return;
	}
	EIC->BindAction(InputConfig->RotateHold, ETriggerEvent::Started, this, &ACPPlayerController::OnRotateHold);
	EIC->BindAction(InputConfig->RotateHold, ETriggerEvent::Completed, this, &ACPPlayerController::OnRotateHold);
	EIC->BindAction(InputConfig->RotateHold, ETriggerEvent::Canceled, this, &ACPPlayerController::OnRotateHold);
	EIC->BindAction(InputConfig->PanHold, ETriggerEvent::Started, this, &ACPPlayerController::OnPanHold);
	EIC->BindAction(InputConfig->PanHold, ETriggerEvent::Completed, this, &ACPPlayerController::OnPanHold);
	EIC->BindAction(InputConfig->PanHold, ETriggerEvent::Canceled, this, &ACPPlayerController::OnPanHold);
	EIC->BindAction(InputConfig->FastHold, ETriggerEvent::Started, this, &ACPPlayerController::OnFast);
	EIC->BindAction(InputConfig->FastHold, ETriggerEvent::Completed, this, &ACPPlayerController::OnFast);
	EIC->BindAction(InputConfig->Zoom, ETriggerEvent::Triggered, this, &ACPPlayerController::OnZoom);
	EIC->BindAction(InputConfig->Move, ETriggerEvent::Triggered, this, &ACPPlayerController::OnMove);
	EIC->BindAction(InputConfig->Move, ETriggerEvent::Completed, this, &ACPPlayerController::OnMove);
	EIC->BindAction(InputConfig->RotateKey, ETriggerEvent::Triggered, this, &ACPPlayerController::OnRotateKey);
	EIC->BindAction(InputConfig->RotateKey, ETriggerEvent::Completed, this, &ACPPlayerController::OnRotateKey);
}

// Hold actions: Started / Completed / Canceled all arrive here; the bool value tells which (true while held).
void ACPPlayerController::OnRotateHold(const FInputActionValue& V)
{
	bRotating = V.Get<bool>();
	GetMousePosition(MouseLast.X, MouseLast.Y);
}

void ACPPlayerController::OnPanHold(const FInputActionValue& V)
{
	bPanning = V.Get<bool>();
	GetMousePosition(MouseLast.X, MouseLast.Y);
}

void ACPPlayerController::OnFast(const FInputActionValue& V)
{
	bFast = V.Get<bool>();
	PushKeyInput();
}

void ACPPlayerController::OnZoom(const FInputActionValue& V)
{
	if (ACPCameraPawn* P = CameraPawn())
	{
		P->AddZoom(V.Get<float>());
	}
}

void ACPPlayerController::OnMove(const FInputActionValue& V)
{
	KeyMove = FVector2D(V.Get<FVector2D>());
	PushKeyInput();
}

void ACPPlayerController::OnRotateKey(const FInputActionValue& V)
{
	KeyRotate = V.Get<float>();
	PushKeyInput();
}

void ACPPlayerController::PushKeyInput()
{
	if (ACPCameraPawn* P = CameraPawn())
	{
		P->SetKeyInput(KeyMove, KeyRotate, bFast);
	}
}

void ACPPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);

	// Mouse drags: deltas of the cursor position while a button is held (keeps the cursor visible and independent of capture).
	double Mx = 0, My = 0;
	if (GetMousePosition(Mx, My))
	{
		const FVector2D Now(Mx, My);
		const FVector2D Delta = Now - MouseLast;
		MouseLast = Now;
		int32 Sx = 0, Sy = 0;
		GetViewportSize(Sx, Sy);
		if (ACPCameraPawn* P = CameraPawn())
		{
			if (bRotating && Sy > 0)
			{
				// OrbitControls: a full viewport height of drag is one turn; dragging right decreases the azimuth, down decreases the polar angle.
				P->AddOrbit(-TwoPi * Delta.X / Sy, -TwoPi * Delta.Y / Sy);
			}
			else if (bPanning)
			{
				P->AddPanDrag(Delta.X, Delta.Y, Sy);
			}
		}
	}

	// Cursor pick through the sim (cp::pick_ground), never Unreal collision.
	FVector Origin, Direction;
	UCPWorldSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UCPWorldSubsystem>() : nullptr;
	bPickValid = Sub && DeprojectMousePositionToWorld(Origin, Direction) && Sub->PickGroundUE(Origin, Direction, Pick);

	SmoothedFrameMs = FMath::Lerp(SmoothedFrameMs, static_cast<double>(DeltaTime) * 1000.0, 0.1);
	if (Hud)
	{
		FCPHudData Data;
		Data.bCursorOnGround = bPickValid;
		Data.SimX = Pick.SimX;
		Data.SimY = Pick.SimY;
		Data.HeightM = Pick.HeightM;
		Data.SlopeRiseRun = Pick.SlopeRiseRun;
		Data.FrameMs = SmoothedFrameMs;
		Hud->SetData(Data);
	}
}

bool ACPPlayerController::GetCursorPick(FCPUEGroundPick& Out) const
{
	if (bPickValid)
	{
		Out = Pick;
	}
	return bPickValid;
}

void ACPPlayerController::CPPick(float FractionX, float FractionY)
{
	int32 Sx = 0, Sy = 0;
	GetViewportSize(Sx, Sy);
	FVector Origin, Direction;
	UCPWorldSubsystem* Sub = GetWorld() ? GetWorld()->GetSubsystem<UCPWorldSubsystem>() : nullptr;
	FCPUEGroundPick Result;
	if (Sub && DeprojectScreenPositionToWorld(FractionX * Sx, FractionY * Sy, Origin, Direction) && Sub->PickGroundUE(Origin, Direction, Result))
	{
		UE_LOG(LogCPGame, Display, TEXT("CPPick %.2f %.2f (viewport %dx%d): sim x %.2f y %.2f height %.2f m slope %.1f %% ; ue %s"),
			FractionX, FractionY, Sx, Sy, Result.SimX, Result.SimY, Result.HeightM, Result.SlopeRiseRun * 100.0, *Result.LocationCm.ToString());
	}
	else
	{
		UE_LOG(LogCPGame, Display, TEXT("CPPick %.2f %.2f (viewport %dx%d): no ground hit"), FractionX, FractionY, Sx, Sy);
	}
}
