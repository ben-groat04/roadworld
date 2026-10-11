#include "Tools/CPToolHudWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"

namespace
{
const FLinearColor PanelColour(0.02f, 0.02f, 0.025f, 0.78f);   // as UCPHudWidget (prototype rgba(13,17,23,0.82))
}

void UCPToolHudWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("ToolCanvas"));
	WidgetTree->RootWidget = Canvas;

	// Top right: the readouts panel.
	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ToolPanel"));
	Panel->SetBrushColor(PanelColour);
	Panel->SetPadding(FMargin(12.f, 8.f));
	Panel->SetVisibility(ESlateVisibility::HitTestInvisible);   // a read-only readout: never swallows a click
	LineBox = WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(), TEXT("ToolLines"));
	Panel->SetContent(LineBox);
	if (UCanvasPanelSlot* PanelSlot = Canvas->AddChildToCanvas(Panel))
	{
		PanelSlot->SetAnchors(FAnchors(1.f, 0.f, 1.f, 0.f));
		PanelSlot->SetAlignment(FVector2D(1.0, 0.0));
		PanelSlot->SetPosition(FVector2D(-16.0, 16.0));
		PanelSlot->SetAutoSize(true);
	}

	// Bottom centre: the key hint.
	CueBar = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("ToolCueBar"));
	CueBar->SetBrushColor(PanelColour);
	CueBar->SetPadding(FMargin(14.f, 6.f));
	CueBar->SetVisibility(ESlateVisibility::Collapsed);
	CueText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("ToolCue"));
	CueText->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.85f, 0.4f, 1.f)));   // accent #ffd866
	CueBar->SetContent(CueText);
	if (UCanvasPanelSlot* CueSlot = Canvas->AddChildToCanvas(CueBar))
	{
		CueSlot->SetAnchors(FAnchors(0.5f, 1.f, 0.5f, 1.f));
		CueSlot->SetAlignment(FVector2D(0.5, 1.0));
		CueSlot->SetPosition(FVector2D(0.0, -24.0));
		CueSlot->SetAutoSize(true);
	}
}

void UCPToolHudWidget::SetLines(const TArray<FCPHudLine>& Lines)
{
	if (!LineBox)
	{
		return;
	}
	// Grow the pool of text blocks when a frame has more lines than ever before; unused ones collapse.
	while (LineBlocks.Num() < Lines.Num())
	{
		UTextBlock* Block = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass());
		LineBox->AddChildToVerticalBox(Block);
		LineBlocks.Add(Block);
	}
	for (int32 I = 0; I < LineBlocks.Num(); ++I)
	{
		UTextBlock* Block = LineBlocks[I];
		if (I < Lines.Num())
		{
			Block->SetVisibility(ESlateVisibility::HitTestInvisible);
			Block->SetText(FText::FromString(Lines[I].Text));
			Block->SetColorAndOpacity(FSlateColor(Lines[I].Colour));
		}
		else
		{
			Block->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	Shown = Lines;
}

void UCPToolHudWidget::SetCue(const FString& Cue)
{
	if (!CueBar || Cue == ShownCue)
	{
		return;
	}
	ShownCue = Cue;
	CueText->SetText(FText::FromString(Cue));
	CueBar->SetVisibility(Cue.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

FString UCPToolHudWidget::GetShownText() const
{
	TArray<FString> Parts;
	for (const FCPHudLine& L : Shown)
	{
		Parts.Add(L.Text);
	}
	return FString::Join(Parts, TEXT("\n"));
}

FString UCPToolHudWidget::GetShownCue() const
{
	return ShownCue;
}
