#include "Tools/CPToolActions.h"

#include "InputAction.h"
#include "InputMappingContext.h"

void UCPToolActions::Build()
{
	auto MakeAction = [this](const FString& Name)
	{
		UInputAction* A = NewObject<UInputAction>(this, *Name);
		A->ValueType = EInputActionValueType::Boolean;
		A->bConsumeInput = false;   // the camera maps the same buttons (right mouse rotates)
		return A;
	};
	Context = NewObject<UInputMappingContext>(this, TEXT("IMC_Tools"));

	Primary = MakeAction(TEXT("IA_ToolPrimary"));
	Secondary = MakeAction(TEXT("IA_ToolSecondary"));
	Context->MapKey(Primary, EKeys::LeftMouseButton);
	Context->MapKey(Secondary, EKeys::RightMouseButton);

	// Keys the manager forwards to the active tool (or handles itself: Z and Y are undo and redo with Ctrl).
	Keys = {EKeys::Enter, EKeys::Escape, EKeys::BackSpace, EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::O,
		EKeys::PageUp, EKeys::PageDown, EKeys::LeftBracket, EKeys::RightBracket, EKeys::Z, EKeys::Y};
	for (const FKey& Key : Keys)
	{
		UInputAction* Action = MakeAction(FString::Printf(TEXT("IA_ToolKey_%s"), *Key.GetFName().ToString()));
		Context->MapKey(Action, Key);
		KeyActions.Add(Action);
	}
}
