#pragma once

// UCPToolActions: builds the Enhanced Input actions and the mapping context of the player tools in C++ (no binary input
// assets), next to UCPInputConfig which does the same for the camera. It only declares which key or button drives which
// action; what the actions do is the tool manager's business. The tool actions do not consume their keys, so the
// camera's actions on the same buttons (right mouse = rotate) still fire.

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "UObject/Object.h"

#include "CPToolActions.generated.h"

class UInputAction;
class UInputMappingContext;

UCLASS()
class UCPToolActions : public UObject
{
	GENERATED_BODY()

public:
	void Build();

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Context;
	UPROPERTY(Transient) TObjectPtr<UInputAction> Primary;     // left mouse button (bool): press and release
	UPROPERTY(Transient) TObjectPtr<UInputAction> Secondary;   // right mouse button (bool): press and release

	/** One bool action per forwarded key, in the same order as Keys. */
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> KeyActions;
	TArray<FKey> Keys;
};
