#pragma once

// UCPHudWidget: the on-screen readout, a UUserWidget whose widget tree is built in C++ (plain dark panel with text).
// It only displays what it is given; it must not query the sim or the camera itself.

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "CPHudWidget.generated.h"

class UTextBlock;

struct FCPHudData
{
	bool bCursorOnGround = false;
	double SimX = 0, SimY = 0;   // metres east / north of the origin
	double HeightM = 0;
	double SlopeRiseRun = 0;
	double FrameMs = 0;
};

UCLASS()
class UCPHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetData(const FCPHudData& Data);
	/** The text currently shown (for logs and tests). */
	FString GetShownText() const;

protected:
	virtual void NativeOnInitialized() override;

private:
	UPROPERTY(Transient) TObjectPtr<UTextBlock> Text;
};
