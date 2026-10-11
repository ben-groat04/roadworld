#include "CPCameraPawn.h"

#include "CPWorldSubsystem.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

namespace
{
// Limits and speeds from prototypes/road-draw/src/scene.js (OrbitControls + updateCamera).
constexpr double MinDistanceM = 15.0, MaxDistanceM = 3800.0;
constexpr double MinPolar = 0.05, MaxPolar = 1.42;
constexpr double ZoomSpeed = 1.1;           // controls.zoomSpeed; one notch scales the distance by 0.95^ZoomSpeed
constexpr double KeyPanPerSecond = 0.9;     // metres per second per metre of distance
constexpr double KeyRotateRate = 1.3;       // radians per second
constexpr double FastFactor = 3.0;
constexpr double CameraAboveGroundM = 3.0;  // the camera never sinks below ground + 3 m
constexpr double TwoPi = 6.283185307179586;
}  // namespace

ACPCameraPawn::ACPCameraPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetFieldOfView(60.f);
}

UCPWorldSubsystem* ACPCameraPawn::WorldSub() const
{
	const UWorld* World = GetWorld();
	return World ? World->GetSubsystem<UCPWorldSubsystem>() : nullptr;
}

void ACPCameraPawn::BeginPlay()
{
	Super::BeginPlay();
	const UCPWorldSubsystem* Sub = WorldSub();
	const FBox2D Bounds = Sub ? Sub->WorldBoundsCm() : FBox2D(ForceInit);
	const FVector2D C = Bounds.bIsValid ? Bounds.GetCenter() : FVector2D::ZeroVector;
	SetView(FVector(C.X, C.Y, 0.0), Distance, Azimuth, Polar);
}

void ACPCameraPawn::SetView(const FVector& TargetCm, double DistanceM, double InAzimuth, double InPolar)
{
	Target = TargetCm;
	Distance = FMath::Clamp(DistanceM, MinDistanceM, MaxDistanceM);
	Azimuth = InAzimuth;
	Polar = FMath::Clamp(InPolar, MinPolar, MaxPolar);
	ApplyRig();
}

void ACPCameraPawn::AddOrbit(double DeltaAzimuth, double DeltaPolar)
{
	Azimuth += DeltaAzimuth;
	Polar = FMath::Clamp(Polar + DeltaPolar, MinPolar, MaxPolar);
}

void ACPCameraPawn::AddPanDrag(double DxPixels, double DyPixels, double ViewportHeight)
{
	if (ViewportHeight < 1.0)
	{
		return;
	}
	// OrbitControls (screenSpacePanning = false): one pixel = 2 * tan(fov/2) * distance / viewportHeight metres, along the
	// ground. Dragging right moves the world right (target left); dragging down moves the target forward.
	const double MetresPerPixel = 2.0 * FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView) * 0.5) * Distance / ViewportHeight;
	const double Fn = FMath::Cos(Azimuth), Fe = -FMath::Sin(Azimuth);   // forward on the ground (north, east)
	const double Rn = FMath::Sin(Azimuth), Re = FMath::Cos(Azimuth);    // right on the ground (north, east)
	const double Right = -DxPixels * MetresPerPixel, Fwd = DyPixels * MetresPerPixel;
	Target.X += (Fn * Fwd + Rn * Right) * 100.0;
	Target.Y += (Fe * Fwd + Re * Right) * 100.0;
}

void ACPCameraPawn::AddZoom(double Notches)
{
	Distance = FMath::Clamp(Distance * FMath::Pow(FMath::Pow(0.95, ZoomSpeed), Notches), MinDistanceM, MaxDistanceM);
}

void ACPCameraPawn::SetKeyInput(const FVector2D& Move, double Rotate, bool bFast)
{
	KeyMove = Move;
	KeyRotate = Rotate;
	bKeyFast = bFast;
}

void ACPCameraPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!KeyMove.IsNearlyZero())
	{
		const double Step = Distance * KeyPanPerSecond * DeltaSeconds * (bKeyFast ? FastFactor : 1.0) * 100.0;
		const double Fn = FMath::Cos(Azimuth), Fe = -FMath::Sin(Azimuth);
		const double Rn = FMath::Sin(Azimuth), Re = FMath::Cos(Azimuth);
		Target.X += (Fn * KeyMove.Y + Rn * KeyMove.X) * Step;
		Target.Y += (Fe * KeyMove.Y + Re * KeyMove.X) * Step;
	}
	if (KeyRotate != 0.0)
	{
		Azimuth += KeyRotate * KeyRotateRate * DeltaSeconds;
	}
	Azimuth = FMath::Fmod(Azimuth, TwoPi);
	ApplyRig();
}

void ACPCameraPawn::ApplyRig()
{
	const UCPWorldSubsystem* Sub = WorldSub();
	const FBox2D Bounds = Sub ? Sub->WorldBoundsCm() : FBox2D(ForceInit);
	if (Bounds.bIsValid)
	{
		// Keep the target inside the world and on the terrain surface.
		Target.X = FMath::Clamp(Target.X, Bounds.Min.X, Bounds.Max.X);
		Target.Y = FMath::Clamp(Target.Y, Bounds.Min.Y, Bounds.Max.Y);
		Target.Z = Sub->GroundZCm(Target.X, Target.Y);
	}
	const double D = Distance * 100.0;
	FVector Offset(-D * FMath::Sin(Polar) * FMath::Cos(Azimuth), D * FMath::Sin(Polar) * FMath::Sin(Azimuth), D * FMath::Cos(Polar));
	FVector Position = Target + Offset;
	if (Bounds.bIsValid)
	{
		Position.Z = FMath::Max(Position.Z, Sub->GroundZCm(Position.X, Position.Y) + CameraAboveGroundM * 100.0);
	}
	SetActorLocationAndRotation(Position, (Target - Position).Rotation());
}
