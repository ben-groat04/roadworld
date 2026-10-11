#pragma once

// FCPPlaytestInput: synthesises the events a real mouse and keyboard would deliver, through Slate's own entry points
// (FSlateApplication::Process*Event), so the game viewport, the input mode, Enhanced Input and the controller all see
// them exactly as if a person had moved the mouse. It does not move the operating system cursor (the machine stays
// usable while a playtest runs) and it knows nothing about what the controls do.

#include "CoreMinimal.h"
#include "InputCoreTypes.h"

class UGameViewportClient;

class FCPPlaytestInput
{
public:
	explicit FCPPlaytestInput(UGameViewportClient* InViewportClient) : ViewportClient(InViewportClient) {}

	/** Pointer position in game viewport pixels (the same units as APlayerController::GetMousePosition). */
	void MoveMouse(const FVector2D& ViewportPixels);
	void MouseButton(const FKey& Button, bool bDown);
	/** Wheel notches; positive = wheel forward (away from the user). */
	void Wheel(float Notches);
	void KeyDown(const FKey& Key);
	void KeyUp(const FKey& Key);

	/** Release everything this object pressed (end of a run, or after a failed step). */
	void ReleaseAll();

	FVector2D Pointer() const { return PointerPx; }

private:
	FVector2D ToAbsolute(const FVector2D& ViewportPixels) const;
	FModifierKeysState Modifiers() const;

	UGameViewportClient* ViewportClient = nullptr;
	FVector2D PointerPx = FVector2D::ZeroVector;
	TSet<FKey> HeldButtons;
	TSet<FKey> HeldKeys;
};
