#pragma once

// ACPCameraPawn: the orbit camera rig of the prototype (a target on the terrain, an azimuth, a polar angle from
// vertical and a distance; limits copied from prototypes/road-draw/src/scene.js). It exposes plain "add" methods
// and applies them in Tick; it does not read input itself (the controller routes input) and does not know about
// meshes or the sim except to ask the world subsystem for the ground height under the target.

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"

#include "CPCameraPawn.generated.h"

class UCameraComponent;
class UCPWorldSubsystem;

UCLASS()
class ACPCameraPawn : public APawn
{
	GENERATED_BODY()

public:
	ACPCameraPawn();

	/** Places the rig. Target is in Unreal centimetres (Z is replaced by the ground height); angles in radians. */
	void SetView(const FVector& TargetCm, double DistanceM, double Azimuth, double Polar);
	void AddOrbit(double DeltaAzimuth, double DeltaPolar);
	/** Drag in screen pixels; ViewportHeight scales it like the prototype's OrbitControls. */
	void AddPanDrag(double DxPixels, double DyPixels, double ViewportHeight);
	/** Wheel notches; positive moves in. */
	void AddZoom(double Notches);
	/** Held keys: Move.X right, Move.Y forward (-1..1), Rotate -1..1, bFast = pan x3. */
	void SetKeyInput(const FVector2D& Move, double Rotate, bool bFast);

	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	FVector GetTargetCm() const { return Target; }
	double GetDistanceM() const { return Distance; }
	double GetAzimuth() const { return Azimuth; }
	double GetPolar() const { return Polar; }
	const UCameraComponent* GetCameraComponent() const { return Camera; }

private:
	void ApplyRig();
	UCPWorldSubsystem* WorldSub() const;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UCameraComponent> Camera;

	FVector Target = FVector::ZeroVector;  // cm, UE frame
	double Azimuth = 0.5;                  // 0 = camera south of the target looking north (prototype az)
	double Polar = 1.0;                    // radians from vertical
	double Distance = 1400.0;              // metres
	FVector2D KeyMove = FVector2D::ZeroVector;
	double KeyRotate = 0;
	bool bKeyFast = false;
};
