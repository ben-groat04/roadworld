// FCPToolSession: converts between the plain structs of CPToolTypes.h and cp::DrawSession. No rules live here.
#include "CPToolSession.h"

#include "CPCommandImpl.h"
#include "CPToolSessionImpl.h"

#include <cmath>
#include <utility>

namespace
{
CPFrame::FD3 ToD3(const cp::Vec3& V)
{
	return {V.x, V.y, V.z};
}

std::vector<CPFrame::FD3> ToD3(const std::vector<cp::Vec3>& V)
{
	std::vector<CPFrame::FD3> Out;
	Out.reserve(V.size());
	for (const cp::Vec3& P : V)
	{
		Out.push_back(ToD3(P));
	}
	return Out;
}

ECPSnapKind ToSnap(cp::SnapKind K)
{
	switch (K)
	{
	case cp::SnapKind::Free: return ECPSnapKind::Free;
	case cp::SnapKind::Angle: return ECPSnapKind::Angle;
	case cp::SnapKind::Guide: return ECPSnapKind::Guide;
	case cp::SnapKind::End: return ECPSnapKind::End;
	case cp::SnapKind::Node: return ECPSnapKind::Node;
	case cp::SnapKind::Road: return ECPSnapKind::Road;
	}
	return ECPSnapKind::Free;
}

ECPIssueCode ToCode(cp::IssueCode C)
{
	switch (C)
	{
	case cp::IssueCode::MinSpan: return ECPIssueCode::MinSpan;
	case cp::IssueCode::Fold: return ECPIssueCode::Fold;
	case cp::IssueCode::CannotFit: return ECPIssueCode::CannotFit;
	case cp::IssueCode::Crossing: return ECPIssueCode::Crossing;
	case cp::IssueCode::NoRegion: return ECPIssueCode::NoRegion;
	case cp::IssueCode::RadiusBelowMinimum: return ECPIssueCode::RadiusBelowMinimum;
	case cp::IssueCode::GradeAboveAdvisory: return ECPIssueCode::GradeAboveAdvisory;
	case cp::IssueCode::BridgeNeeded: return ECPIssueCode::BridgeNeeded;
	case cp::IssueCode::TunnelNeeded: return ECPIssueCode::TunnelNeeded;
	case cp::IssueCode::G1Fallback: return ECPIssueCode::G1Fallback;
	}
	return ECPIssueCode::CannotFit;
}

std::vector<FCPPreviewIssue> ToIssues(const std::vector<cp::Issue>& In)
{
	std::vector<FCPPreviewIssue> Out;
	for (const cp::Issue& I : In)
	{
		Out.push_back({ToCode(I.code), I.text});
	}
	return Out;
}

cp::PointerInput ToPointer(const FCPPointerInput& P)
{
	cp::PointerInput In;
	In.ray.origin = {P.OriginSim.X, P.OriginSim.Y, P.OriginSim.Z};
	In.ray.dir = {P.DirSim.X, P.DirSim.Y, P.DirSim.Z};
	In.metres_per_pixel_per_metre = P.MetresPerPixelPerMetre;
	return In;
}

cp::Modifiers ToMods(const FCPToolModifiers& M)
{
	cp::Modifiers Out;
	Out.snap_off = M.bSnapOff;
	return Out;
}

FCPToolFinishResult ToFinish(cp::FinishResult&& F)
{
	FCPToolFinishResult Out;
	Out.bOk = F.ok();
	Out.Error = std::move(F.error);
	if (F.command)
	{
		Out.Command.reset(new FCPCommand{std::move(F.command), std::string()});
		Out.Command->Name = Out.Command->Command->name();
	}
	return Out;
}
}  // namespace

void FCPCommandDeleter::operator()(FCPCommand* Command) const
{
	delete Command;
}

std::string CPCommandName(const FCPCommandHandle& Command)
{
	return Command ? Command->Name : std::string();
}

FCPToolSession::FCPToolSession(std::unique_ptr<FImpl> InImpl) : Impl(std::move(InImpl)) {}
FCPToolSession::~FCPToolSession() = default;

FCPToolPreview FCPToolSession::Hover(const FCPPointerInput& Pointer, const FCPToolModifiers& Modifiers)
{
	const cp::DrawPreview Pv = Impl->Session.hover(ToPointer(Pointer), ToMods(Modifiers));
	FCPToolPreview Out;
	Out.bHasPick = Pv.has_pick;
	Out.bStrokeActive = Impl->Session.active();
	Out.HeightOffsetM = Impl->Session.height_offset();
	Out.bOneWay = Impl->Session.one_way();
	Out.RoadTypeId = Impl->Session.road_type();
	if (Impl->Region)
	{
		if (const cp::RoadType* Type = Impl->Region->road_type(Out.RoadTypeId))
		{
			Out.RoadTypeName = Type->name;
			Out.bRoadTypeDual = Type->dual;
		}
	}
	if (!Pv.has_pick)
	{
		return Out;
	}
	Out.Cursor = ToD3(Pv.cursor);
	Out.Snap = ToSnap(Pv.snap);
	Out.Guide.Kind = Pv.guide.type == cp::SnapGuide::Type::Line ? ECPGuideKind::Line : (Pv.guide.type == cp::SnapGuide::Type::Offset ? ECPGuideKind::Offset : ECPGuideKind::None);
	Out.Guide.Polyline = ToD3(Pv.guide.polyline);
	Out.Guide.bFaint = Pv.guide.faint;
	Out.Clicked = ToD3(Pv.clicked);
	for (const cp::Ribbon& R : Pv.ribbons)
	{
		Out.Ribbons.push_back({ToD3(R.committed), ToD3(R.hover), R.width});
	}
	Out.Errors = ToIssues(Pv.errors);
	Out.Advisories = ToIssues(Pv.advisories);
	Out.bWouldFinish = Pv.would_finish;
	const cp::Readouts& R = Pv.readouts;
	Out.Readouts = {R.length, R.span_length, R.min_radius, R.end_radius, R.max_grade_pct, R.grade_pct, R.height, R.heading_deg};

	// While the cursor sits on the last placed point the sim returns no ribbon at all (GAP-4 in docs/unreal-ui-parity.md):
	// keep showing the previous one so the ghost does not vanish under the cursor.
	if (!Out.Ribbons.empty())
	{
		Impl->LastRibbons = Out.Ribbons;
	}
	else if (Pv.would_finish && !Impl->LastRibbons.empty())
	{
		Out.Ribbons = Impl->LastRibbons;
		Out.bRibbonsHeld = true;
	}
	return Out;
}

FCPToolClickResult FCPToolSession::Click(const FCPPointerInput& Pointer, const FCPToolModifiers& Modifiers)
{
	cp::ClickResult R = Impl->Session.click(ToPointer(Pointer), ToMods(Modifiers));
	if (!R.added || R.finished)
	{
		Impl->LastRibbons.clear();   // an added point keeps the ribbons of the hover that led to it: the cursor now sits on it and the sim returns none
	}
	FCPToolClickResult Out;
	Out.bAdded = R.added;
	Out.bFinished = R.finished;
	Out.bAutoFinished = R.auto_finished;
	Out.Errors = ToIssues(R.errors);
	Out.Finish = ToFinish(std::move(R.finish));
	return Out;
}

FCPToolFinishResult FCPToolSession::Finish()
{
	Impl->LastRibbons.clear();
	return ToFinish(Impl->Session.finish());
}

void FCPToolSession::Cancel()
{
	Impl->LastRibbons.clear();
	Impl->Session.cancel();
}

bool FCPToolSession::UndoPoint()
{
	Impl->LastRibbons.clear();
	return Impl->Session.undo_point();
}

bool FCPToolSession::IsActive() const { return Impl->Session.active(); }
int FCPToolSession::PointCount() const { return static_cast<int>(Impl->Session.points().size()); }
void FCPToolSession::SetRoadType(const std::string& RoadTypeId) { Impl->Session.set_road_type(RoadTypeId); }
const std::string& FCPToolSession::RoadType() const { return Impl->Session.road_type(); }
void FCPToolSession::SetOneWay(bool bOneWay) { Impl->Session.set_one_way(bOneWay); }
bool FCPToolSession::OneWay() const { return Impl->Session.one_way(); }
void FCPToolSession::AdjustHeight(double DeltaM) { Impl->Session.adjust_height(DeltaM); }
double FCPToolSession::HeightOffsetM() const { return Impl->Session.height_offset(); }
void FCPToolSession::Invalidate() { Impl->Session.invalidate(); }
