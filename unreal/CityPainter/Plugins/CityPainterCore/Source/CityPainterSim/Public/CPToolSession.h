#pragma once

// FCPToolSession: the road-drawing tool as the engine sees it. A thin wrapper over cp::DrawSession: a pointer ray goes
// in, plain preview structs come out, and a finished stroke leaves as an opaque command handle for the sim world to
// execute (one undo step per stroke). The session never changes the graph and holds no rules of its own: snapping,
// fitting, validation and the readouts all come from sim_core. Plain C++, sim frame (see CPToolTypes.h).
// Created and owned by FCPSimWorld (FCPSimWorld::ToolSession()). It must not know about Unreal types, input or drawing.

#include "CPToolTypes.h"

#include <memory>
#include <string>

class CITYPAINTERSIM_API FCPToolSession
{
public:
	struct FImpl;
	explicit FCPToolSession(std::unique_ptr<FImpl> InImpl);  // built by FCPSimWorld only
	~FCPToolSession();
	FCPToolSession(const FCPToolSession&) = delete;
	FCPToolSession& operator=(const FCPToolSession&) = delete;

	// ---- pointer ----
	/** Everything to show for a pointer ray; does not change the stroke. */
	FCPToolPreview Hover(const FCPPointerInput& Pointer, const FCPToolModifiers& Modifiers);
	/** Places a point where Hover would show the cursor; a click on the last point, or onto a road end / node / road, ends the stroke. */
	FCPToolClickResult Click(const FCPPointerInput& Pointer, const FCPToolModifiers& Modifiers);

	// ---- stroke ----
	/** Ends the stroke at the last clicked point (Enter, right-click). The stroke stays open on error. */
	FCPToolFinishResult Finish();
	void Cancel();
	/** Backspace: drops the last point; false when there is none. */
	bool UndoPoint();
	bool IsActive() const;
	int PointCount() const;

	// ---- tool state ----
	void SetRoadType(const std::string& RoadTypeId);
	const std::string& RoadType() const;
	void SetOneWay(bool bOneWay);
	bool OneWay() const;
	/** PgUp / PgDn: raises or lowers the next point; reset by every click. */
	void AdjustHeight(double DeltaM);
	double HeightOffsetM() const;

	/** Called by the sim world after it changed the graph (the session's road index is a snapshot). */
	void Invalidate();

private:
	std::unique_ptr<FImpl> Impl;
};
