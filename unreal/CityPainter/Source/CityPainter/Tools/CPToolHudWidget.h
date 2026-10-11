#pragma once

// UCPToolHudWidget: the road tool's on-screen text, a UUserWidget whose tree is built in C++: a panel at the top left (under the M1 readout)
// with one coloured line per readout / error / advisory, and a key-hint bar at the bottom centre. It only shows the lines
// it is given (CPToolHudFormat builds them); it never swallows mouse clicks. It must not query the sim or the tools.

#include "Blueprint/UserWidget.h"
#include "CoreMinimal.h"

#include "Tools/CPToolHudFormat.h"

#include "CPToolHudWidget.generated.h"

class UBorder;
class UTextBlock;
class UVerticalBox;

UCLASS()
class UCPToolHudWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	void SetLines(const TArray<FCPHudLine>& Lines);
	/** Empty hides the bar. */
	void SetCue(const FString& Cue);
	/** The lines currently shown, joined with newlines (for logs and tests). */
	FString GetShownText() const;
	FString GetShownCue() const;

protected:
	virtual void NativeOnInitialized() override;

private:
	UPROPERTY(Transient) TObjectPtr<UVerticalBox> LineBox;
	UPROPERTY(Transient) TArray<TObjectPtr<UTextBlock>> LineBlocks;
	UPROPERTY(Transient) TObjectPtr<UBorder> CueBar;
	UPROPERTY(Transient) TObjectPtr<UTextBlock> CueText;
	TArray<FCPHudLine> Shown;
	FString ShownCue;
};
