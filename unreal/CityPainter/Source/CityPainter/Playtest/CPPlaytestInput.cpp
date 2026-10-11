#include "CPPlaytestInput.h"

#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/Events.h"
#include "Widgets/SViewport.h"

FVector2D FCPPlaytestInput::ToAbsolute(const FVector2D& ViewportPixels) const
{
	const TSharedPtr<SViewport> Widget = ViewportClient ? ViewportClient->GetGameViewportWidget() : nullptr;
	if (!Widget.IsValid() || !ViewportClient->Viewport)
	{
		return ViewportPixels;
	}
	const FGeometry Geo = Widget->GetCachedGeometry();
	const FIntPoint Size = ViewportClient->Viewport->GetSizeXY();
	const FVector2D Local(ViewportPixels.X * Geo.GetLocalSize().X / FMath::Max(1, Size.X), ViewportPixels.Y * Geo.GetLocalSize().Y / FMath::Max(1, Size.Y));
	return FVector2D(Geo.LocalToAbsolute(FVector2f(Local)));
}

FModifierKeysState FCPPlaytestInput::Modifiers() const
{
	return FModifierKeysState(HeldKeys.Contains(EKeys::LeftShift), HeldKeys.Contains(EKeys::RightShift), false, false, false, false, false, false, false);
}

void FCPPlaytestInput::MoveMouse(const FVector2D& ViewportPixels)
{
	const FVector2D Last = ToAbsolute(PointerPx), Now = ToAbsolute(ViewportPixels);
	PointerPx = ViewportPixels;
	FPointerEvent Move(0, Now, Last, HeldButtons, EKeys::Invalid, 0.f, Modifiers());
	FSlateApplication::Get().ProcessMouseMoveEvent(Move);
}

void FCPPlaytestInput::MouseButton(const FKey& Button, bool bDown)
{
	const FVector2D P = ToAbsolute(PointerPx);
	if (bDown)
	{
		HeldButtons.Add(Button);
		const TSharedPtr<SWindow> Window = ViewportClient ? ViewportClient->GetWindow() : nullptr;
		FPointerEvent Down(0, P, P, HeldButtons, Button, 0.f, Modifiers());
		FSlateApplication::Get().ProcessMouseButtonDownEvent(Window.IsValid() ? Window->GetNativeWindow() : nullptr, Down);
	}
	else
	{
		HeldButtons.Remove(Button);
		FPointerEvent Up(0, P, P, HeldButtons, Button, 0.f, Modifiers());
		FSlateApplication::Get().ProcessMouseButtonUpEvent(Up);
	}
}

void FCPPlaytestInput::Wheel(float Notches)
{
	const FVector2D P = ToAbsolute(PointerPx);
	FPointerEvent Wheel(0, P, P, HeldButtons, EKeys::MouseScrollUp, Notches, Modifiers());
	FSlateApplication::Get().ProcessMouseWheelOrGestureEvent(Wheel, nullptr);
}

void FCPPlaytestInput::KeyDown(const FKey& Key)
{
	HeldKeys.Add(Key);
	FSlateApplication::Get().ProcessKeyDownEvent(FKeyEvent(Key, Modifiers(), 0, false, 0, 0));
}

void FCPPlaytestInput::KeyUp(const FKey& Key)
{
	HeldKeys.Remove(Key);
	FSlateApplication::Get().ProcessKeyUpEvent(FKeyEvent(Key, Modifiers(), 0, false, 0, 0));
}

void FCPPlaytestInput::ReleaseAll()
{
	for (const FKey& K : TArray<FKey>(HeldKeys.Array()))
	{
		KeyUp(K);
	}
	for (const FKey& B : TArray<FKey>(HeldButtons.Array()))
	{
		MouseButton(B, false);
	}
}
