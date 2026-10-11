#pragma once

// UCPTool: the base of every player tool (road drawing now; edit, roundabout, bulldoze later). A tool receives what the
// player does as plain calls (hover with a pointer ray, click, secondary click, cancel, a key) and answers with feedback
// to show (a preview in Unreal's frame, a refusal text). It forwards to one sim session and holds no rules of its own.
// It must not know about Enhanced Input, widgets or the overlay component: the tool manager routes input in and
// feedback out.

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/Object.h"

#include "CPToolBridge.h"

#include "CPTool.generated.h"

class UCPWorldSubsystem;

/** What the manager hands a tool with every call. */
struct FCPToolContext
{
	UCPWorldSubsystem* World = nullptr;
	bool bHasRay = false;                    // the pointer is inside the viewport
	FVector RayOriginCm = FVector::ZeroVector;
	FVector RayDirectionUE = FVector::ZeroVector;
	double MetresPerPixelPerMetre = 0.0;     // ground metres per screen pixel per metre of distance
	bool bSnapOff = false;                   // Shift held
	double TimeSeconds = 0.0;
};

/** What a tool wants shown. */
struct FCPToolFeedback
{
	FCPUEToolPreview Preview;
	bool bHasPreview = false;
	bool bSnapOff = false;
	FString RefusalText;                     // a refused click / finish / edit; stays until the next action or its timeout
};

UCLASS(Abstract)
class UCPTool : public UObject
{
	GENERATED_BODY()

public:
	virtual FName GetToolName() const PURE_VIRTUAL(UCPTool::GetToolName, return NAME_None;);
	virtual void OnActivated(const FCPToolContext& Context) {}
	virtual void OnDeactivated(const FCPToolContext& Context) {}

	/** The pointer moved, the camera moved or the world changed. */
	virtual void OnHover(const FCPToolContext& Context) {}
	/** Primary button released without dragging. */
	virtual void OnClick(const FCPToolContext& Context) {}
	/** Secondary button released without dragging (a drag is the camera's). */
	virtual void OnSecondaryClick(const FCPToolContext& Context) {}
	/** Escape. */
	virtual void OnCancel(const FCPToolContext& Context) {}
	/** Any other key the manager forwards (Enter, Backspace, 1-4, O, PageUp, PageDown, brackets). */
	virtual void OnKey(const FKey& Key, const FCPToolContext& Context) {}

	const FCPToolFeedback& GetFeedback() const { return Feedback; }
	/** Increases whenever the tool's own state changed (so the manager knows the preview is stale even if the pointer did not move). */
	int32 GetRevision() const { return Revision; }

protected:
	FCPToolFeedback Feedback;
	int32 Revision = 0;
};
