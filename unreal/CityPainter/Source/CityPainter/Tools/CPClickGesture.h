#pragma once

// FCPClickGesture: tells a click from a drag for one mouse button. Press records where the pointer was; Release says
// whether it ended a click (the pointer moved at most MaxTravelPx while the button was down). The same rule serves both
// buttons: a left click places a point (a drag does nothing), a right CLICK finishes the road while a right DRAG is the
// camera's rotate. The prototype ignores a press that moved more than 4 px (docs/unreal-ui-parity.md section B).
// Plain data: no input system, no pointer reading (the caller passes positions).

#include "CoreMinimal.h"

struct FCPClickGesture
{
	static constexpr double DefaultMaxTravelPx = 4.0;

	void Press(const FVector2D& PointerPx)
	{
		Start = PointerPx;
		bDown = true;
	}

	/** True when this release ends a click. Safe to call without a press (returns false). */
	bool Release(const FVector2D& PointerPx, double MaxTravelPx = DefaultMaxTravelPx)
	{
		const bool bClick = bDown && FVector2D::Distance(Start, PointerPx) <= MaxTravelPx;
		bDown = false;
		return bClick;
	}

	void Reset() { bDown = false; }
	bool IsDown() const { return bDown; }

private:
	FVector2D Start = FVector2D::ZeroVector;
	bool bDown = false;
};
