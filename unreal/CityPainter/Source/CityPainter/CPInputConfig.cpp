#include "CPInputConfig.h"

#include "EnhancedActionKeyMapping.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

void UCPInputConfig::Build()
{
	auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* A = NewObject<UInputAction>(this, Name);
		A->ValueType = Type;
		return A;
	};
	RotateHold = MakeAction(TEXT("IA_RotateHold"), EInputActionValueType::Boolean);
	PanHold = MakeAction(TEXT("IA_PanHold"), EInputActionValueType::Boolean);
	FastHold = MakeAction(TEXT("IA_FastHold"), EInputActionValueType::Boolean);
	Zoom = MakeAction(TEXT("IA_Zoom"), EInputActionValueType::Axis1D);
	Move = MakeAction(TEXT("IA_Move"), EInputActionValueType::Axis2D);
	RotateKey = MakeAction(TEXT("IA_RotateKey"), EInputActionValueType::Axis1D);

	Context = NewObject<UInputMappingContext>(this, TEXT("IMC_Camera"));
	Context->MapKey(RotateHold, EKeys::RightMouseButton);
	Context->MapKey(PanHold, EKeys::MiddleMouseButton);
	Context->MapKey(FastHold, EKeys::LeftShift);
	Context->MapKey(Zoom, EKeys::MouseWheelAxis);
	Context->MapKey(RotateKey, EKeys::E);
	Context->MapKey(RotateKey, EKeys::Q).Modifiers.Add(NewObject<UInputModifierNegate>(Context));

	// A 1D key becomes a 2D axis: forward/back go on Y through a swizzle, back and left are negated.
	auto MapMove = [this](const FKey& Key, bool bOnY, bool bNegate)
	{
		FEnhancedActionKeyMapping& M = Context->MapKey(Move, Key);
		if (bOnY)
		{
			M.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(Context));  // default order YXZ
		}
		if (bNegate)
		{
			M.Modifiers.Add(NewObject<UInputModifierNegate>(Context));
		}
	};
	MapMove(EKeys::W, true, false);
	MapMove(EKeys::Up, true, false);
	MapMove(EKeys::S, true, true);
	MapMove(EKeys::Down, true, true);
	MapMove(EKeys::D, false, false);
	MapMove(EKeys::Right, false, false);
	MapMove(EKeys::A, false, true);
	MapMove(EKeys::Left, false, true);
}
