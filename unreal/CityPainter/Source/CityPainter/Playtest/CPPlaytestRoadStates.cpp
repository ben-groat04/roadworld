// The milestone 2 "every state" playtest: draws, with real mouse and key events, the states the prototype screenshots
// show (docs/progress/2026-10-11-prototype-ui/02..28) and takes one picture of each at a prototype-like oblique
// framing, so each can be set beside its prototype screenshot. Each state also logs the tool HUD text
// ("CPPlaytest M2 HUD [label]: ...") so the words can be read without opening the image.
//
// Layout (sim metres east / north of the world centre): R0 is an arched road (-100,0) (0,12) (100,0); the other
// strokes are drawn south of it (north is up in the pictures).

#include "CPPlaytestRoads.h"

#include "CPPlaytestRoadSteps.h"

namespace
{
using namespace CPPlaytestRoadSteps;

constexpr double Dist = 340.0;      // metres, the prototype's mid-stroke screenshots are about this wide
constexpr double Polar = 0.95;      // radians from straight down (the prototype's start view)

/** One state: pointer position, a settle, a log of the HUD, a picture. */
void State(FBuilder& B, const FString& Name, double East, double North)
{
	B.Pointer(East, North);
	B.Wait(4);
	B.LogHud(Name);
	B.Shot(FString::Printf(TEXT("20_st_%s"), *Name));
}

void StateHere(FBuilder& B, const FString& Name)
{
	B.Wait(4);
	B.LogHud(Name);
	B.Shot(FString::Printf(TEXT("20_st_%s"), *Name));
}

void ScenarioStates(FBuilder& B)
{
	B.View(Dist, 0.0, Polar, 0.0, -80.0);

	// 02 idle: the pointer is over the sky or the HUD, not over the ground (viewport top edge).
	B.PointerFraction(0.5, 0.0);
	StateHere(B, TEXT("s02_idle"));

	// 03 hover, free.
	State(B, TEXT("s03_hover_free"), 0, -45);

	// R0, the road the snap states point at: arched, three points, Shift held so the points land where aimed.
	B.ShiftDown();
	B.Click(-100, 0);
	B.Click(0, 12);
	B.Click(100, 0);
	B.Press(EKeys::Enter);
	B.ShiftUp();
	B.CheckGraph(TEXT("states: R0 drawn"), 2, 1, TEXT(""));

	// 05 / 06 / 07 / 08 / 09: the snap kinds with no stroke open.
	State(B, TEXT("s05_snap_end"), 100, 0);
	State(B, TEXT("s06_snap_road"), 0, 12);
	State(B, TEXT("s07_snap_extension_guide"), 140, -6);
	State(B, TEXT("s08_snap_offset_guide"), 0, 40);
	State(B, TEXT("s08b_snap_offset_guide_other"), -45, 50);
	State(B, TEXT("s09_hover_free_second"), 0, 95);

	// 04 / 10 / 11: a stroke with the angle and length snap, then with Shift (snap off).
	B.Click(-70, -80);
	B.Click(-30, -55);
	State(B, TEXT("s04_midstroke"), 40, -30);
	B.Press(EKeys::Escape);
	B.Click(-50, -90);
	State(B, TEXT("s10_snap_angle_length"), 0, -75);
	B.ShiftDown();
	State(B, TEXT("s11_shift_snap_off"), -2, -77);
	// 12 min span: the cursor within 4 m of the only point, Shift held (so no snap to the length grid).
	B.ShiftUp();
	B.Press(EKeys::Escape);
	B.ShiftDown();
	B.Click(60, -100);
	State(B, TEXT("s12_error_min_span"), 62, -100.8);
	B.Press(EKeys::Escape);
	// 13 too tight: two points 10 m apart and a hairpin back.
	B.Click(50, -130);
	B.Click(60, -130);
	State(B, TEXT("s13_error_too_tight"), 60, -121);
	B.Press(EKeys::Escape);
	B.ShiftUp();

	// 14 radius advisory (B road): a bend tighter than comfortable.
	B.Press(EKeys::Two);
	B.Click(-80, -150);
	B.Click(-40, -130);
	State(B, TEXT("s14_advisory_radius"), -30, -100);
	B.Press(EKeys::Escape);
	B.Press(EKeys::One);

	// 15 grade advisory: residential street, raised 10 m over 40 m (Shift+PgUp = 5 m steps).
	B.ShiftDown();
	B.Click(100, -130);
	B.Press(EKeys::PageUp);
	B.Press(EKeys::PageUp);
	State(B, TEXT("s15_advisory_grade"), 100, -90);
	B.Press(EKeys::Escape);
	// 16 bridge: the cursor 25 m above the ground.
	B.Click(-120, -140);
	for (int32 I = 0; I < 5; ++I) { B.Press(EKeys::PageUp); }
	State(B, TEXT("s16_advisory_bridge"), -30, -140);
	B.Press(EKeys::Escape);
	// 17 tunnel: the cursor 25 m below the ground.
	B.Click(-120, -150);
	for (int32 I = 0; I < 5; ++I) { B.Press(EKeys::PageDown); }
	State(B, TEXT("s17_advisory_tunnel"), -30, -150);
	B.Press(EKeys::Escape);
	B.ShiftUp();

	// 18 / 19 / 20 / 21 / 22: road types and one-way. The same two clicks for each, with the pointer on the third spot.
	auto TypeStroke = [&B](const FKey& Type, const FString& Name, bool bOneWay)
	{
		B.Press(Type);
		if (bOneWay) { B.Press(EKeys::O); }
		B.Click(-90, -150);
		B.Click(-50, -135);
		State(B, Name, 0, -105);
		B.Press(EKeys::Escape);
		if (bOneWay) { B.Press(EKeys::O); }
	};
	TypeStroke(EKeys::Three, TEXT("s18_type3_dual_ghost"), false);
	TypeStroke(EKeys::Four, TEXT("s19_type4_motorway_ghost"), false);
	TypeStroke(EKeys::Three, TEXT("s20_type3_single_carriageway_O"), true);
	TypeStroke(EKeys::One, TEXT("s21_type1_oneway"), true);
	TypeStroke(EKeys::Two, TEXT("s22_type2_b_road_ghost"), false);
	B.Press(EKeys::One);

	// 26 three points before the finish, then 27 finished and idle.
	B.ShiftDown();
	B.Click(-100, -165);
	B.Click(0, -165);
	B.Click(100, -165);
	B.ShiftUp();
	State(B, TEXT("s26_stroke_3pts_before_finish"), 100, -165);
	B.Press(EKeys::Enter);
	B.CheckGraph(TEXT("states: second road finished"), 4, 2, TEXT(""));
	State(B, TEXT("s27_finished_road_idle"), 100, -165);
	B.View(130.0, 0.0, 0.2, 0.0, -165.0);
	StateHere(B, TEXT("s27b_finished_topdown"));
	B.View(90.0, 0.5, 1.2, 60.0, -165.0);
	StateHere(B, TEXT("s27c_finished_low_oblique"));
	B.View(Dist, 0.0, Polar, 0.0, -80.0);

	// 28 crossing refused: a street across R0 whose height differs by a few metres where they cross.
	B.ShiftDown();
	B.Click(-20, -40);
	B.Press(EKeys::PageUp);
	B.Click(-20, 60);
	B.Press(EKeys::Enter);
	B.ShiftUp();
	StateHere(B, TEXT("s28_error_crossing_too_close"));
	B.Press(EKeys::Escape);

	// Slope report: a road over the hills west of the centre (ground about 50 m up there). Earthworks are not wired yet, so this
	// shows what a road on a slope looks like today.
	B.ShiftDown();
	B.View(170.0, 0.0, 1.05, -240.0, -40.0);
	B.Click(-300, -90);
	B.Click(-240, -40);
	B.Click(-180, 10);
	B.Press(EKeys::Enter);
	B.ShiftUp();
	B.Pointer(-240, 60);
	StateHere(B, TEXT("s30_road_on_slope"));
	B.View(120.0, 0.6, 1.3, -240.0, -40.0);
	StateHere(B, TEXT("s31_road_on_slope_low"));
}

void ScenarioUndoAll(FBuilder& B)
{
	// Back to the empty world for the counted scenarios that follow.
	for (int32 I = 0; I < 8; ++I)
	{
		B.Chord(EKeys::LeftControl, EKeys::Z);
	}
	B.CheckGraph(TEXT("states: everything undone"), 0, 0, TEXT(""));
}
}  // namespace

void AppendCPPlaytestRoadStates(TArray<FCPPlaytestStep>& Out)
{
	FBuilder B{Out};
	ScenarioStates(B);
	ScenarioUndoAll(B);
}
