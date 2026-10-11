#pragma once

// UCPRoadDrawTool: the click-click road tool. It forwards the pointer, clicks and keys to the sim's tool session
// (FCPToolSession) and submits the finished stroke to the world subsystem as one command. Snapping, fitting,
// validation and the readouts all come from the sim; this class only translates calls and keeps the refusal text
// visible. It must not contain road rules, input bindings or drawing code.

#include "Tools/CPTool.h"

#include "CPRoadDrawTool.generated.h"

class FCPToolSession;

UCLASS()
class UCPRoadDrawTool : public UCPTool
{
	GENERATED_BODY()

public:
	virtual FName GetToolName() const override { return FName(TEXT("RoadDraw")); }
	virtual void OnActivated(const FCPToolContext& Context) override;
	virtual void OnDeactivated(const FCPToolContext& Context) override;
	virtual void OnHover(const FCPToolContext& Context) override;
	virtual void OnClick(const FCPToolContext& Context) override;
	virtual void OnSecondaryClick(const FCPToolContext& Context) override;
	virtual void OnCancel(const FCPToolContext& Context) override;
	virtual void OnKey(const FKey& Key, const FCPToolContext& Context) override;

private:
	FCPToolSession* Session(const FCPToolContext& Context) const;
	/** Ends the stroke at the last clicked point and submits it; shows why when the sim refuses. */
	void FinishStroke(const FCPToolContext& Context);
	void SelectRoadTypeSlot(int32 Slot, const FCPToolContext& Context);
	void Refuse(const FString& Text, const FCPToolContext& Context);
	void ClearRefusalOnAction();
	void Touch() { ++Revision; }

	double RefusalExpires = 0.0;   // world time at which the refusal text goes away
};
