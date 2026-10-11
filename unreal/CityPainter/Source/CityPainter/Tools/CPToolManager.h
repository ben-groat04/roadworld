#pragma once

// UCPToolManager: owns the player tools and routes input to the active one. It binds the tool Enhanced Input actions,
// turns the cursor into a pointer ray every tick, tells a click from a drag (left click places, right CLICK finishes,
// right DRAG is the camera's), forwards keys, handles undo / redo (Ctrl+Z, Ctrl+Y / Ctrl+Shift+Z) through the world
// subsystem, and pushes the active tool's feedback to the overlay (render) and the tool HUD. The player controller holds
// one of these and calls three hooks (Begin, Tick, and Create from SetupInputComponent). It must not contain road rules
// (tools forward to the sim) nor camera maths.

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "Tools/CPClickGesture.h"
#include "Tools/CPTool.h"

#include "CPToolManager.generated.h"

class APlayerController;
class UCPRoadDrawTool;
class UCPToolActions;
class UCPToolHudWidget;
class UEnhancedInputComponent;

UCLASS()
class UCPToolManager : public UObject
{
	GENERATED_BODY()

public:
	/** Builds the manager for a controller and binds its tool actions to the controller's input component. */
	static UCPToolManager* Create(APlayerController* Controller, UEnhancedInputComponent* Input);

	/** Controller BeginPlay (local player): mapping context, HUD, first tool. */
	void Begin();
	/** Controller PlayerTick: hover, overlay and HUD refresh. */
	void Tick(float DeltaTime);

	UCPTool* GetActiveTool() const { return ActiveTool; }
	/** The tool HUD text currently shown (for the playtest and logs). */
	FString GetHudText() const;

	/** Plain entry points (the input bindings call these; the playtest and tests may too). */
	void DoUndo();
	void DoRedo();

private:
	void BindInput(UEnhancedInputComponent* Input);
	void OnPrimary(bool bPressed);
	void OnSecondary(bool bPressed);
	void OnKeyAction(FKey Key);
	void SetActiveTool(UCPTool* Tool);

	FCPToolContext MakeContext() const;
	bool PointerPx(FVector2D& Out) const;
	void ShowStatus(const FString& Text);

	UPROPERTY(Transient) TObjectPtr<APlayerController> Controller;
	UPROPERTY(Transient) TObjectPtr<UCPToolActions> Actions;
	UPROPERTY(Transient) TObjectPtr<UCPRoadDrawTool> RoadDraw;
	UPROPERTY(Transient) TObjectPtr<UCPTool> ActiveTool;
	UPROPERTY(Transient) TObjectPtr<UCPToolHudWidget> Hud;

	FCPClickGesture PrimaryGesture, SecondaryGesture;

	// Hover is recomputed only when one of these changed since the last tick (a still pointer costs nothing).
	struct FHoverSignature
	{
		FVector2D Pointer = FVector2D(-1, -1);
		FVector CameraLocation = FVector::ZeroVector;
		FRotator CameraRotation = FRotator::ZeroRotator;
		int32 ToolRevision = -1;
		uint64 WorldVersion = 0;
		bool bSnapOff = false;
		bool operator==(const FHoverSignature& O) const
		{
			return Pointer == O.Pointer && CameraLocation == O.CameraLocation && CameraRotation == O.CameraRotation && ToolRevision == O.ToolRevision && WorldVersion == O.WorldVersion && bSnapOff == O.bSnapOff;
		}
	};
	FHoverSignature LastSignature;
	FString StatusText;                 // undo / redo outcome, shown for a few seconds
	double StatusExpires = 0.0;
	bool bBegun = false;
};
