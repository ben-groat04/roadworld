#pragma once

// UCPInputConfig: builds the Enhanced Input actions and the mapping context for the camera in C++ (no binary input
// assets), so the whole input scheme is diffable. It only declares which key drives which action; it must not know
// what the actions do (the controller binds them) or anything about the sim.

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "CPInputConfig.generated.h"

class UInputAction;
class UInputMappingContext;

UCLASS()
class UCPInputConfig : public UObject
{
	GENERATED_BODY()

public:
	void Build();

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Context;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RotateHold;   // right mouse button (bool)
	UPROPERTY(Transient) TObjectPtr<UInputAction> PanHold;      // middle mouse button (bool)
	UPROPERTY(Transient) TObjectPtr<UInputAction> FastHold;     // shift (bool): pan x3
	UPROPERTY(Transient) TObjectPtr<UInputAction> Zoom;         // mouse wheel (axis1D)
	UPROPERTY(Transient) TObjectPtr<UInputAction> Move;         // WASD / arrows (axis2D: x right, y forward)
	UPROPERTY(Transient) TObjectPtr<UInputAction> RotateKey;    // Q / E (axis1D)
};
