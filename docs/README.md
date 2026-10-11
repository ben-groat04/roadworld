# docs/ index

Entry point for a new session: **HANDOFF.md**, then `CLAUDE.md` (repo root), then `planning-log.md`. Last revised 11 Oct 2026 (afternoon).

## Start here
- `HANDOFF.md` – current state, the owner's standing rules, branch and worktree map, review scores, ordered next steps. Current, rewritten whenever a session ends.
- `planning-log.md` – source of truth for every product and design decision (Rounds 1-48). Current.

## Decisions and scope
- `v0-brief.md` – v0 kickoff brief (scope, build order). Historical: v0 scope is also summarised in the planning log.
- `road-graph-model.md` – agreed road graph data model (edges only, vertical profiles, overrides). Current.
- `junctions.md` – junction generator interface and design notes (incl. turn pockets, section 7b). Current.
- `road-marking-rules.md` – UK marking rules, draft from Traffic Signs Manual ch. 5. Reference only; numbers are tuned by eye.

## Design references
- `performance-plan.md` – Unreal 5.8 performance plan for runtime-generated roads and terrain. Current, measurement order matters.
- `vegetation-and-surface-realism.md` – Nanite, Infinigen and the "sterile look" problem. Reference.
- `sim-performance-baseline.md` – first sim_core timings (partial, one busy-laptop run). Dated.
- `repo-health-review.md` – cold-read review of repo structure (rated fair) with the fix list. Current as a to-do source.
- `reference/` – private real-world photos (gitignored, never commit); each folder has an INDEX.

## Unreal
- `unreal-architecture.md` – **the structure reference for the Unreal phase** (module layering, sim-to-render contract, conventions, two-week plan). Adopted in planning log Round 34. Current.
- `unreal-game-plan.md` – **the plan for the road builder as an Unreal game** (inventory, module structure, data flow, milestones M1-M8, sim_core gaps, risks). Adopted 11 Oct 2026 (Round 40). Current.
- `unreal-ui-parity.md` – spec of the browser prototype's UI (15 elements, 31 inputs, 18 feedback items, tuning panel, parity checklist, gaps), the owner's target UI for the game (Round 43). Screenshots in `progress/2026-10-11-prototype-ui/`. Current.
- `unreal-photorealism.md` – what fits the look target in UE 5.8. Reference; partly amended by `research/ue58-feature-sweep.md`.
- `windows-setup.md` – Visual Studio/MSVC setup for Unreal C++. Historical (done on this machine).
- `../unreal/tools/README.md` and `../unreal/tools/KNOWN_RISKS.md` – build and editor automation, predicted first-compile errors.

## Research (`research/`, dated 10 Oct 2026, each ends in a verdict)
- `research/ue58-feature-sweep.md` – UE 5.8 features our docs missed, MCP comparison, Realtime Mesh Component pricing.
- `research/pcg-city-research.md` – Epic's PCG City Sample; buildings stay in sim_core, PCG for dressing only.
- `research/mesh-terrain-research.md` – Mesh Terrain: verdict stays "avoid" (editor-only modifiers).
- `research/pre-research.md` – v0 licence and data pre-research (CGAL ruled out, Clipper2). Historical.
- `research/reference-claims-audit.md` – 11 Oct 2026 audit of every doc that says what real roads or the reference photos look like: claims, verdicts, files opened, edits, open hits.
- `research/vegetation-plan.md` – 11 Oct 2026: what 36 opened photos show, world cover vs road-edge scatter, free asset routes, reviewer ignore/judge lists.
- `research/civil-engine-research.md` – 11 Oct 2026: Civil Engine and Road Creator Pro (Fab listing and manual); verdict do not buy, ideas noted (Round 47).
- `research/infinigen-preflight.md` – 11 Oct 2026: read-only Infinigen pre-flight (machine facts, install route); dropped by the person.

## Progress
- `progress/2026-10-10-structures/` – first structure and earthwork renders: `INDEX.md` (scenarios) and `REVIEW.md` (cold-read score 4.5/10). Snapshot; later rounds get their own dated folder.
- `progress/2026-10-10-structures-r2/`, `-r3/`, `-r4/` – later structure rounds (r3 and r4 have `REVIEW.md`; the r4 review rejects stacked terraces as a ziggurat).
- `progress/2026-10-10-unreal-first/`, `2026-10-11-unreal-second/`, `2026-10-11-unreal-third/` – first Unreal screenshots, the winding fix, the material and lighting pass (second and third have `REVIEW.md`; photorealism 2 -> 3.5/10). Fourth pass (`2026-10-11-unreal-fourth/`, with `REVIEW.md` 4.5/10 and `LUMEN_FINDINGS.md`) and fifth pass (`2026-10-11-unreal-fifth/`, marking wiring fix, not reviewed).
- `progress/2026-10-11-unreal-m1/` – Unreal milestone 1: `STATUS.md`, `INDEX.md` and five sandbox captures; `2026-10-11-unreal-m1-playtest/` – agent playtest run 1 (36 checks, results.json, game log, screenshots).
- `progress/2026-10-11-structures-r6/` – earthworks round 6 renders and reference notes (rounds 7-8 live on branch `agent/grading-r6`, see HANDOFF §8); `2026-10-10-structures-r5/` has the r5 review (5/10).
- `progress/2026-10-11-test-world/` – seeded 2 km test world previews and cold read (overall 3/10); `2026-10-11-vegetation-kit/` – Blender vegetation kit sheets and review (4/10).
- `progress/2026-10-11-prototype-ui/` – 44 screenshots of the browser prototype for the UI parity spec.
- Unmerged work keeps its progress folders in the worktrees (`../roadworld-wt-*`): `2026-10-11-junction-markings`, `2026-10-11-world-cover-r2` (r3 review `REVIEW_R3.md`) and `-r4`, `2026-10-11-structures-r7`/`-r8`. HANDOFF §8 maps them.
- `progress/2026-10-11-junction-fixes/` – before/after renders of splitter islands, give-way lines and slip gores.

## Reference claims (rule)
- Any statement about what the reference photos show (colour, wear, proportions, vegetation, light) must name the reference files that were actually opened for it, e.g. "(gsv 180248, street/02)". Text in an INDEX is not an opened reference.
- Review briefs for cold readers must require this: list the files opened, and mark anything not seen in a photo as "unverified against references".
- A score or verdict based on fewer than two opened references for that view must say so next to the score.
- Numbers derived from such claims (albedo, wear percentages, patch densities) carry the same rule; the audit in `research/reference-claims-audit.md` shows what went wrong when it was skipped.
