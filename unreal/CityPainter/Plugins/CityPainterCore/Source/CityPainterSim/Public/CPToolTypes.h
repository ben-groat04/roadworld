#pragma once

// Plain data of the road-drawing tool: what a pointer looks like going in, and what the preview, a click and a finish
// look like coming out. Everything is in the SIM frame (metres, x east, y north, z up, doubles); the World module turns
// it into Unreal's frame. No Unreal types, no sim_core types. It must not know how the data is drawn or shown.

#include "CPCommand.h"
#include "CPFrame.h"
#include "CPSimApi.h"

#include <cstdint>
#include <string>
#include <vector>

struct FCPPointerInput
{
	CPFrame::FD3 OriginSim;                 // pointer ray origin
	CPFrame::FD3 DirSim;                    // pointer ray direction (need not be unit length)
	double MetresPerPixelPerMetre = 0;      // 2 tan(fov/2) / viewport height: ground metres per screen pixel per metre of distance
};

struct FCPToolModifiers
{
	bool bSnapOff = false;                  // Shift held
};

enum class ECPSnapKind : std::uint8_t { Free, Angle, Guide, End, Node, Road };
enum class ECPGuideKind : std::uint8_t { None, Line, Offset };
enum class ECPIssueCode : std::uint8_t
{
	MinSpan, Fold, CannotFit, Crossing, NoRegion,                                        // errors
	RadiusBelowMinimum, GradeAboveAdvisory, BridgeNeeded, TunnelNeeded, G1Fallback,      // advisories
};

struct FCPPreviewIssue
{
	ECPIssueCode Code = ECPIssueCode::CannotFit;
	std::string Text;
};

struct FCPPreviewGuide
{
	ECPGuideKind Kind = ECPGuideKind::None;
	std::vector<CPFrame::FD3> Polyline;     // draped on the ground
	bool bFaint = false;                    // the angle / length grid line
};

struct FCPPreviewRibbon
{
	std::vector<CPFrame::FD3> Committed;    // fitted centreline up to the last clicked point
	std::vector<CPFrame::FD3> Hover;        // last clicked point to the cursor
	double WidthM = 0;                      // full width of the carriageway the ribbon stands for (sim_core Ribbon::width)
};

struct FCPPreviewReadouts
{
	double LengthM = 0, SpanLengthM = 0;
	double MinRadiusM = 0, EndRadiusM = 0;  // infinity on a straight
	double MaxGradePct = 0, GradePct = 0;
	double HeightM = 0;                     // cursor height
	double HeadingDeg = 0;                  // compass degrees (0 north, clockwise) at the cursor
};

struct FCPToolPreview
{
	bool bHasPick = false;                  // the ray hit something; the rest is meaningless otherwise
	CPFrame::FD3 Cursor;                    // resolved (snapped) cursor point
	ECPSnapKind Snap = ECPSnapKind::Free;
	FCPPreviewGuide Guide;
	std::vector<CPFrame::FD3> Clicked;      // points placed so far
	std::vector<FCPPreviewRibbon> Ribbons;  // one per carriageway; empty before the first click
	bool bRibbonsHeld = false;              // the ribbons are the previous hover's (the sim returns none when the cursor is on the last point)
	std::vector<FCPPreviewIssue> Errors;    // hard: a click is refused
	std::vector<FCPPreviewIssue> Advisories;
	FCPPreviewReadouts Readouts;
	bool bWouldFinish = false;              // the cursor is on the last placed point: a click finishes the stroke
	bool bStrokeActive = false;
	double HeightOffsetM = 0;               // PgUp / PgDn offset of the next point
	bool bOneWay = false;
	std::string RoadTypeId, RoadTypeName;
	bool bRoadTypeDual = false;             // a dual type draws two carriageways unless one-way is on

	bool Ok() const { return bHasPick && Errors.empty(); }
};

struct FCPToolFinishResult
{
	bool bOk = false;
	std::string Error;                      // why not (kept visible by the host)
	FCPCommandHandle Command;               // set when bOk: the host submits it to the sim world
};

struct FCPToolClickResult
{
	bool bAdded = false;                    // a point was placed
	bool bFinished = false;                 // the stroke ended: Finish is set
	bool bAutoFinished = false;             // ... because the click snapped onto a road end / node / road
	std::vector<FCPPreviewIssue> Errors;    // why nothing was added
	FCPToolFinishResult Finish;             // when bFinished, or when an attempted finish was refused (Error)
};

struct FCPRoadTypeInfo
{
	std::string Id, Name;
	bool bDual = false;
	bool bTwoWay = false;
	bool bRoundaboutRing = false;           // not a type the player draws
};
