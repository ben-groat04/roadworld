#pragma once

// ACPPlayerController: routes input to the camera pawn and turns the mouse position into a ground pick. The cursor is
// shown; the pick is a ray from DeprojectMousePositionToWorld handed to the world subsystem (analytic, no Unreal
// collision). It feeds the HUD. It must not contain camera maths (the pawn) or terrain maths (the sim).

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "CPHudWidget.h"
#include "CPWorldSubsystem.h"

#include "CPPlayerController.generated.h"

class ACPCameraPawn;
class UCPInputConfig;
class UCPToolManager;   // M2 tools hook
struct FInputActionValue;

UCLASS()
class ACPPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ACPPlayerController();

	virtual void BeginPlay() override;
	virtual void PlayerTick(float DeltaTime) override;

	/** Latest cursor pick; false when the cursor is not over the ground. */
	bool GetCursorPick(FCPUEGroundPick& Out) const;

	/** The text the HUD shows right now (for the playtest and logs). */
	FString GetHudText() const { return Hud ? Hud->GetShownText() : FString(); }

	/** Console: logs the ground pick at a screen point given as fractions of the viewport (0.5 0.5 = centre). */
	UFUNCTION(Exec)
	void CPPick(float FractionX, float FractionY);

protected:
	virtual void SetupInputComponent() override;

private:
	void OnRotateHold(const FInputActionValue& V);
	void OnPanHold(const FInputActionValue& V);
	void OnZoom(const FInputActionValue& V);
	void OnMove(const FInputActionValue& V);
	void OnRotateKey(const FInputActionValue& V);
	void OnFast(const FInputActionValue& V);
	void PushKeyInput();
	ACPCameraPawn* CameraPawn() const;

	UPROPERTY(Transient) TObjectPtr<UCPInputConfig> InputConfig;
	UPROPERTY(Transient) TObjectPtr<UCPHudWidget> Hud;

	// ---- M2 tools hook (see Tools/CPToolManager.h): input, hover, overlay and HUD of the player tools ----
	UPROPERTY(Transient) TObjectPtr<UCPToolManager> Tools;

	bool bRotating = false, bPanning = false, bFast = false;
	FVector2D MouseLast = FVector2D::ZeroVector;
	FVector2D KeyMove = FVector2D::ZeroVector;
	double KeyRotate = 0;
	bool bPickValid = false;
	FCPUEGroundPick Pick;
	double SmoothedFrameMs = 16.7;
};
