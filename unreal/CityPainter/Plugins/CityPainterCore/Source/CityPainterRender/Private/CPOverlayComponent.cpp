#include "CPOverlayComponent.h"

#include "CPRenderSettings.h"
#include "CityPainterRender.h"

#include "Components/LineBatchComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
// Colours and sizes from docs/unreal-ui-parity.md (A, E7-E10; hex values are sRGB).
FLinearColor Srgb(uint8 R, uint8 G, uint8 B, float Alpha = 1.f)
{
	FLinearColor C = FLinearColor::FromSRGBColor(FColor(R, G, B));
	C.A = Alpha;
	return C;
}
const FLinearColor GhostWhite = Srgb(0xdd, 0xe6, 0xf0, 0.55f);
const FLinearColor GhostRed = Srgb(0xe5, 0x48, 0x4d, 0.55f);
const FLinearColor RingGreen = Srgb(0x3f, 0xb9, 0x50);   // end, road, node
const FLinearColor RingCyan = Srgb(0x7e, 0xe0, 0xff);    // guide
const FLinearColor RingWhite = Srgb(0xff, 0xff, 0xff);   // angle, free
const FLinearColor DotGold = Srgb(0xff, 0xd8, 0x66);     // placed points, finish marker
const FLinearColor GuideFaint = Srgb(0xff, 0xff, 0xff, 0.5f);

constexpr float GhostLiftCm = 35.f;     // ribbon 0.35 m above the centreline samples
constexpr float RingLiftCm = 50.f;      // ring 0.5 m above the ground
constexpr float DotLiftCm = 40.f;
constexpr double RingFreePx = 7.0, RingSnapPx = 11.0, FinishPx = 16.0;
constexpr float DotSizePx = 8.f;        // diameter (4 px radius)
constexpr double DashCm = 400.0, GapCm = 300.0;   // guides: 4 m dash, 3 m gap
constexpr int32 RingSides = 28;
const uint8 Foreground = static_cast<uint8>(SDPG_Foreground);

FLinearColor RingColour(ECPSnapKind Snap)
{
	switch (Snap)
	{
	case ECPSnapKind::End:
	case ECPSnapKind::Road:
	case ECPSnapKind::Node: return RingGreen;
	case ECPSnapKind::Guide: return RingCyan;
	default: return RingWhite;
	}
}

float SignedAreaUp(const std::array<float, 3>& A, const std::array<float, 3>& B, const std::array<float, 3>& C)
{
	// Picture seen from above: UE X is north (up), UE Y is east (right); counter-clockwise = positive (as the terrain test).
	return (B[1] - A[1]) * (C[0] - A[0]) - (B[0] - A[0]) * (C[1] - A[1]);
}

/** Appends a flat strip of the given full width along a polyline (UE cm), vertices relative to Origin, faces up. */
void AppendStrip(FCPMeshSection& M, const TArray<FVector>& Raw, double HalfWidthCm, const FVector& Origin)
{
	TArray<FVector> Pts;
	for (const FVector& P : Raw)
	{
		if (Pts.Num() == 0 || FVector::DistSquared2D(P, Pts.Last()) > 1.0)
		{
			Pts.Add(P);
		}
	}
	if (Pts.Num() < 2)
	{
		return;
	}
	const std::int32_t Base = static_cast<std::int32_t>(M.Position.size());
	for (int32 I = 0; I < Pts.Num(); ++I)
	{
		const FVector Prev = Pts[FMath::Max(I - 1, 0)], Next = Pts[FMath::Min(I + 1, Pts.Num() - 1)];
		FVector2D Dir(Next.X - Prev.X, Next.Y - Prev.Y);
		Dir = Dir.GetSafeNormal();
		const FVector Left(-Dir.Y * HalfWidthCm, Dir.X * HalfWidthCm, 0.0);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FVector V = Pts[I] + (Side == 0 ? Left : -Left) + FVector(0, 0, GhostLiftCm) - Origin;
			M.Position.push_back({static_cast<float>(V.X), static_cast<float>(V.Y), static_cast<float>(V.Z)});
			M.Normal.push_back({0.f, 0.f, 1.f});
			M.TangentX.push_back({0.f, 1.f, 0.f});
			M.UV0.push_back({0.f, 0.f});
		}
	}
	auto Tri = [&M](std::int32_t A, std::int32_t B, std::int32_t C)
	{
		const bool bUp = SignedAreaUp(M.Position[A], M.Position[B], M.Position[C]) > 0.f;
		M.Indices.insert(M.Indices.end(), bUp ? std::initializer_list<std::int32_t>{A, B, C} : std::initializer_list<std::int32_t>{A, C, B});
	};
	for (int32 I = 0; I + 1 < Pts.Num(); ++I)
	{
		const std::int32_t L0 = Base + 2 * I, R0 = L0 + 1, L1 = L0 + 2, R1 = L0 + 3;
		Tri(L0, R0, L1);
		Tri(R0, R1, L1);
	}
}
}  // namespace

UCPOverlayComponent::UCPOverlayComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UCPOverlayComponent::EnsureObjects()
{
	if (bObjectsReady)
	{
		return;
	}
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	bObjectsReady = true;
	Lines = NewObject<ULineBatchComponent>(Owner, TEXT("OverlayLines"));
	Lines->SetupAttachment(this);
	Lines->RegisterComponent();

	const UCPRenderSettings* Settings = GetDefault<UCPRenderSettings>();
	UMaterialInterface* Base = Settings->GhostMaterial.LoadSynchronous();
	if (!Base)
	{
		UE_LOG(LogCPRender, Error, TEXT("Ghost material %s not found; the tool ghost will use the default material (opaque, no colour)"), *Settings->GhostMaterial.ToString());
		return;
	}
	GhostNormal = UMaterialInstanceDynamic::Create(Base, this);
	GhostNormal->SetVectorParameterValue(TEXT("Color"), GhostWhite);
	GhostError = UMaterialInstanceDynamic::Create(Base, this);
	GhostError->SetVectorParameterValue(TEXT("Color"), GhostRed);
}

void UCPOverlayComponent::OnComponentDestroyed(bool bDestroyingHierarchy)
{
	if (Ribbon)
	{
		Ribbon->Clear();
		Ribbon.Reset();
	}
	if (Lines)
	{
		Lines->DestroyComponent();
		Lines = nullptr;
	}
	bObjectsReady = false;
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

void UCPOverlayComponent::Clear()
{
	if (Lines)
	{
		Lines->Flush();
	}
	if (Ribbon)
	{
		Ribbon->ClearSection(0);
	}
}

void UCPOverlayComponent::SetPreview(const FCPUEToolPreview& Preview)
{
	EnsureObjects();
	Clear();
	if (!Preview.bHasPick || !Lines)
	{
		return;
	}
	DrawRibbon(Preview);
	DrawMarks(Preview);
}

void UCPOverlayComponent::DrawRibbon(const FCPUEToolPreview& Preview)
{
	FCPMeshSection M;
	bool bOrigin = false;
	FVector Origin = FVector::ZeroVector;
	for (const FCPUEPreviewRibbon& R : Preview.Ribbons)
	{
		TArray<FVector> Pts = R.CommittedCm;
		for (int32 I = 0; I < R.HoverCm.Num(); ++I)
		{
			Pts.Add(R.HoverCm[I]);
		}
		if (Pts.Num() < 2)
		{
			continue;
		}
		if (!bOrigin)
		{
			Origin = Pts[0];
			bOrigin = true;
			M.OriginUeCm[0] = Origin.X;
			M.OriginUeCm[1] = Origin.Y;
			M.OriginUeCm[2] = Origin.Z;
		}
		AppendStrip(M, Pts, R.WidthM * 50.0, Origin);   // half of the full width, in cm
	}
	if (M.IsEmpty())
	{
		return;
	}
	AActor* Owner = GetOwner();
	if (!Ribbon && Owner)
	{
		Ribbon = ICPTileMesh::Create(*Owner, FName(TEXT("OverlayRibbon")));
	}
	if (Ribbon)
	{
		// Red only for errors; advisories (amber) leave the ghost white (docs/unreal-ui-parity.md C.1, C.4).
		UMaterialInterface* Material = Preview.HasErrors() ? GhostError.Get() : GhostNormal.Get();
		Ribbon->SetSection(0, M, Material);
	}
}

void UCPOverlayComponent::DrawMarks(const FCPUEToolPreview& Preview)
{
	const double CmPerPx = FMath::Max(Preview.MetresPerPixel, 0.01) * 100.0;

	// Guides: dashed, 4 m dash and 3 m gap measured along the line.
	const TArray<FVector>& G = Preview.Guide.PolylineCm;
	if (Preview.Guide.Kind != ECPGuideKind::None && G.Num() >= 2)
	{
		const FLinearColor Colour = Preview.Guide.bFaint ? GuideFaint : RingCyan;
		double Phase = 0.0;   // distance into the current dash+gap period
		for (int32 I = 0; I + 1 < G.Num(); ++I)
		{
			const FVector A = G[I], B = G[I + 1];
			const double Len = FVector::Dist(A, B);
			if (Len < 1e-6)
			{
				continue;
			}
			const FVector Dir = (B - A) / Len;
			double S = 0.0;
			while (S < Len)
			{
				const double PeriodPos = FMath::Fmod(Phase + S, DashCm + GapCm);
				const bool bDash = PeriodPos < DashCm;
				const double Step = FMath::Min(Len - S, (bDash ? DashCm : DashCm + GapCm) - PeriodPos);
				if (bDash)
				{
					Lines->DrawLine(A + Dir * S, A + Dir * (S + Step), Colour, Foreground, 0.f);
				}
				S += FMath::Max(Step, 1e-3);
			}
			Phase = FMath::Fmod(Phase + Len, DashCm + GapCm);
		}
	}

	// Placed points.
	for (const FVector& P : Preview.ClickedCm)
	{
		Lines->DrawPoint(P + FVector(0, 0, DotLiftCm), DotGold, DotSizePx, Foreground);
	}

	// Finish marker: a gold ring on the last placed point while a click there would finish the stroke.
	if (Preview.bWouldFinish && Preview.ClickedCm.Num() > 0)
	{
		const FVector C = Preview.ClickedCm.Last() + FVector(0, 0, RingLiftCm);
		Lines->DrawCircle(C, FVector::XAxisVector, FVector::YAxisVector, DotGold, static_cast<float>(FinishPx * CmPerPx), RingSides, Foreground);
		Lines->DrawCircle(C, FVector::XAxisVector, FVector::YAxisVector, DotGold, static_cast<float>(FinishPx * CmPerPx * 0.86), RingSides, Foreground);
		return;   // the cursor is on that point: the snap ring would only cover it
	}

	// Snap ring: three concentric circles (outer radius 1, inner 0.72 of the nominal size), flat on the ground.
	const double Px = Preview.Snap == ECPSnapKind::Free ? RingFreePx : RingSnapPx;
	const double R = Px * CmPerPx;
	const FVector C = Preview.CursorCm + FVector(0, 0, RingLiftCm);
	const FLinearColor Colour = RingColour(Preview.Snap);
	for (const double K : {1.0, 0.86, 0.72})
	{
		Lines->DrawCircle(C, FVector::XAxisVector, FVector::YAxisVector, Colour, static_cast<float>(R * K), RingSides, Foreground);
	}
}
