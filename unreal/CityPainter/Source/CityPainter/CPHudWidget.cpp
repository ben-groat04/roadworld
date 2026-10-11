#include "CPHudWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"

void UCPHudWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	if (!WidgetTree || WidgetTree->RootWidget)
	{
		return;
	}
	UCanvasPanel* Canvas = WidgetTree->ConstructWidget<UCanvasPanel>(UCanvasPanel::StaticClass(), TEXT("Canvas"));
	WidgetTree->RootWidget = Canvas;

	UBorder* Panel = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("Panel"));
	Panel->SetBrushColor(FLinearColor(0.02f, 0.02f, 0.025f, 0.78f));
	Panel->SetPadding(FMargin(12.f, 8.f));
	// A read-only readout: the panel must never swallow mouse clicks (a drag that starts over it still orbits).
	Panel->SetVisibility(ESlateVisibility::HitTestInvisible);

	Text = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("Readout"));
	Text->SetColorAndOpacity(FSlateColor(FLinearColor(0.92f, 0.92f, 0.9f, 1.f)));
	Text->SetText(FText::FromString(TEXT("City Painter")));
	{
		FSlateFontInfo Font = Text->GetFont();
		Font.Size = 12;
		Text->SetFont(Font);
	}
	Panel->SetContent(Text);

	if (UCanvasPanelSlot* CanvasSlot = Canvas->AddChildToCanvas(Panel))
	{
		CanvasSlot->SetAutoSize(true);
		CanvasSlot->SetAnchors(FAnchors(0.f, 1.f, 0.f, 1.f));   // bottom left, above the key hint: the top left belongs to the road tool HUD (as the prototype)
		CanvasSlot->SetAlignment(FVector2D(0.0, 1.0));
		CanvasSlot->SetPosition(FVector2D(16.0, -84.0));
	}
}

void UCPHudWidget::SetData(const FCPHudData& D)
{
	if (!Text)
	{
		return;
	}
	FString Line1;
	if (D.bCursorOnGround)
	{
		const double Deg = FMath::RadiansToDegrees(FMath::Atan(D.SlopeRiseRun));
		Line1 = FString::Printf(TEXT("x %.2f m   y %.2f m   height %.2f m   slope %.1f %% (%.1f deg)"), D.SimX, D.SimY, D.HeightM, D.SlopeRiseRun * 100.0, Deg);
	}
	else
	{
		Line1 = TEXT("cursor not over the ground");
	}
	const FString Line2 = FString::Printf(TEXT("frame %.1f ms (%.0f fps)"), D.FrameMs, D.FrameMs > 0.0 ? 1000.0 / D.FrameMs : 0.0);
	Text->SetText(FText::FromString(Line1 + TEXT("\n") + Line2));
}

FString UCPHudWidget::GetShownText() const
{
	return Text ? Text->GetText().ToString() : FString();
}
