# Repo organisation audit (11 Oct 2026, evening). Read-only; nothing was changed except this file.

Checked: git metadata, directory counts and sizes, the docs named in the brief. Not opened: source, images, agents' STATUS files. "Merged" below means `git merge-base --is-ancestor`.

## (a) Worst problems, plainly

1. Main (`claude/practical-ride-rrf8rx`, 2764203) is 33 commits behind `integ/2026-10-11` (265bd1a). 30+ commits of finished work (world cover r4, junction markings, grading r7/r8, road tool r2, rules fixes) are not on main. Seven new agents were branched from the old integ tip e0c0d4b, so the longer main waits the more merge conflicts they all carry.
2. 20 worktrees exist (19 registered plus an orphan folder). Seven are fully merged or superseded and were already listed for removal in QUEUE "Small fixes" and HANDOFF §8 many hours ago. None was removed.
3. 53 `build*` dirs (3.8 GB) in the main checkout, 38 older than today, plus 4 more `roadworld-*` build trees (about 0.8 GB) and 5 logs sitting next to the repo. repo-health-review said "delete 23 build dirs" on 10 Oct; there are now more than double.
4. Four milestone tables disagree: `unreal-game-plan.md` §C, `HANDOFF.md` §4, `QUEUE.md` "The plan", plus the per-branch story in QUEUE "Running now". HANDOFF §4/§7/§8/§12 still describe the state before the M2 playtest (HEAD 269fa1e, "no own commits"), and QUEUE itself is at "Small fixes" listing a task that was never started.
5. Branch ownership is unclear: `m2`, `m3ui` and `uelook` all sit on the same commit 62368f3 (three branches, one result). `sliproad` and `tjunction` worktrees exist at e0c0d4b although the plan says slip road waits for the roundabout round. `veg-kit-r2` is 41 commits behind main and QUEUE says the kit "stops" (owner chose free assets) but nothing says to drop the branch.
6. `.git` is 1.8 GB (repo-health-review measured 186 MB on 10 Oct); 953 LFS files, SourceArt on disk 920 MB. The LFS quota warning in repo-health-review ("confirm quota before committing more art") was never actioned. Two more untracked asset folders (`SourceArt/Free`, `SourceArt/Meshes/Free`) are about to be added.
7. `docs/progress` is 533 MB, 19 folders on main plus more inside worktrees, with 3 naming schemes (`YYYY-MM-DD-name`, `-rN` suffix, `-playtest` suffix). Only 4 of 19 have a STATUS file, 9 have a REVIEW/AUDIT file, 8 have neither. Sources of truth for "what was accepted" are split across `ACCEPTANCE.md` (13 lines), per-folder REVIEW files, QUEUE and HANDOFF §9.
8. Scratch outside the repo: `C:\Users\bgbg0\cp_scratch` (249 MB, 312 files; holds world-cover r4 composites that QUEUE says belong in `docs/reference/_compare/`), `livetest`, `hk`, `roadworld-wt-hooktest` (an empty orphan, not a git worktree), `roadworld-b-tool2-*`, `roadworld-junc-*`, `roadworld-build-tool`. Nothing says who owns them or when they die.
9. `docs/` entry points still duplicate: README lists "Rounds 1-49" and omits QUEUE-linked files; HANDOFF says "decisions up to Round 48"; planning log is at Round 50. The rules were consolidated in the afternoon, but HANDOFF §10, CLAUDE.md and AGENT_CONTRACT still restate each other in places.
10. Code-structure recommendations from repo-health-review (god files, public RoadGraph mutators, tests untagged, `prototypes/road-draw/x/` and generated `index.html` tracked, no eol rule) are almost all still open; `markings.cpp` grew 2586 -> 2645, `structure_mesh.cpp` 1675 -> 1761. They were only re-copied into QUEUE "Small fixes" as one "carried over, not started" line.

## (b) Recommended before, not done

| # | Recommendation (source) | Status | Evidence |
|---|---|---|---|
| 1 | Delete the `build-*` dirs (repo-health "cosmetic", 1.8 GB / 23 dirs) | NOT DONE, worse | 53 dirs, 3.8 GB in main; 38 older than today |
| 2 | Remove merged worktrees `-wt-tool`, `-wt-world`, `-wt-grade`... (HANDOFF §8 "leftover, can be removed"; QUEUE "Small fixes") | NOT DONE | `-wt-tool` and `-wt-world` merged into main long ago; still registered |
| 3 | Per-agent worktrees from current main via script (rules-audit H7) | PARTLY | `new_agent_worktree.ps1` exists with the right usage line; but 6 worktrees branched from stale e0c0d4b, `veg-kit-r2` from 269fa1e (41 behind), `grade-r9` 15 behind |
| 4 | One normal green-CI commit, no "WIP" default (repo-health #7) | PARTLY | CI deliberately off (`[skip ci]`, owner rule); last CI-verified commit still 1ae80b0 |
| 5 | Short `docs/README.md` index, research into `docs/research/` (repo-health docs section) | DONE then drifted | `research/` exists with 10 files; README stale (Round 49, no QUEUE/ACCEPTANCE entry for new work, no `progress/2026-10-11-gate-notices`, `-hooks`, `-unreal-fifth`) |
| 6 | HANDOFF rewritten as short "current state", regenerated at each stop (repo-health) | NOT DONE | 170 lines, §4/§7/§8/§12 are pre-playtest; QUEUE "Small fixes" says refresh after integration |
| 7 | Mark `v0-brief` and `pre-research` historical | DONE | README says "Historical" |
| 8 | Single source for rules; HANDOFF/log point at the contract (rules-audit H1,H2,H8) | MOSTLY DONE | AGENT_CONTRACT.md (20 lines), CLAUDE.md gates, templates, `make_brief`, `check_handin`, hooks live. HANDOFF §1 still quotes rules; rules-fix-mapping lists 5 "Resolved, owner to confirm" items still unconfirmed (QUEUE Waiting on owner) |
| 9 | Hand-in check script, brief generator, STATUS template (H3-5) | DONE | `tools/make_brief.mjs`, `check_handin.mjs`, `docs/templates/` |
| 10 | Memory notes `dont-rush-a-stop`, `think-about-parallelism` (H13) | NOT DONE | memory dir has 20 files, none matching |
| 11 | Move cp_scratch composites into `docs/reference/_compare/world-cover-r4/`, delete the outside folder with owner OK (QUEUE) | NOT DONE | `cp_scratch` 249 MB still there |
| 12 | Remove leftover `unreal/.../Plugins/CityPainterSim` (QUEUE) | NOT DONE | folder (Binaries/Intermediate, ignored) still present |
| 13 | Extract `cp/geom.h`, migrate helpers (repo-health #1) | PARTLY | `geom.h` exists, included by 10 of 31 src files |
| 14 | Move taper table and `kGoreLength` to region pack (#3) | DONE for gore (kGoreLength gone, TODO gone); other tunables (~40 structure dims, grading/structure defaults) NOT | QUEUE "Small fixes" |
| 15 | Split `markings.cpp`, `structure_mesh.cpp`, `render_mesh.cpp` (#2) | NOT DONE | 2645 / 1761 / 1632 lines |
| 16 | `RoadGraph` mutators command-only (#5) | NOT DONE | `road_graph.h:75` `insert_node` still public |
| 17 | Test tags fast/golden/fuzz, timings (#4) | NOT DONE | no labels in `tests/CMakeLists.txt` |
| 18 | `* text=auto eol=lf` in `.gitattributes` (#6) | NOT DONE | file has LFS lines only |
| 19 | Move `prototypes/road-draw/x/` and screenshots out of git, untrack generated `index.html` (#8) | NOT DONE | 11 tracked in `x/`, `index.html` tracked |
| 20 | Confirm LFS quota before more art | NOT DONE | 953 LFS objects, `.git` 1.8 GB, more art incoming |
| 21 | Acceptance log per round (H11) | PARTLY | `docs/ACCEPTANCE.md` exists, 13 lines; most accepted rounds not in it |
| 22 | "Owner to confirm" 5 settled contradictions (rules-fix-mapping) | NOT DONE | QUEUE "Waiting on the owner" |
| 23 | Pier clearance 0.3 -> 0.2 m region data | NOT DONE | QUEUE |

## (c) Inventory

### Worktrees (all tips dated 11 Oct; ahead = commits not on main)

| Worktree dir | Branch | Tip | Ahead of main | In integ? | Dirty files | Builds | Verdict |
|---|---|---|---|---|---|---|---|
| (main) roadworld | claude/practical-ride-rrf8rx | 2764203 | 0 | n/a | 3 untracked | 53 / 3.8 GB | keep |
| -wt-integ | integ/2026-10-11 | e0c0d4b worktree (branch tip 265bd1a incl. main) | 33 | self | 0 | 7 / 0.2 GB | keep until main ff |
| -wt-tool | agent/road-tool | 250759a | 0 (merged) | yes | 0 | 4 | STALE, remove |
| -wt-world | agent/test-world | b47f5e8 | 0 (merged) | yes | 0 | 6 / 0.2 GB | STALE, remove |
| -wt-rules | agent/rules-fix | 84a6cb8 | 0 (merged) | no (main only) | 0 | 4 | STALE, remove |
| -wt-grade | agent/grading-r6 | 2aff581 | 14 | yes | 0 | 12 / 0.7 GB | merged via integ; remove after main ff |
| -wt-cover | agent/world-cover-r2 | 0da3c74 | 9 | yes | 0 | 7 / 0.2 GB | merged via integ; remove after main ff |
| -wt-tool2 | agent/road-tool-2 | d7dfc99 | 1 | yes | 0 | 4 | merged via integ; remove after main ff |
| -wt-junc | agent/junction-markings | be59b50 | 5 | yes except audit commit be59b50 (docs) | 0 | 4 | merged via integ; AUDIT.md lives only here: copy first |
| -wt-gradelayer | agent/gradelayer | e0c0d4b | 32 (= integ) | yes | 10 | 6 | RUNNING (do not touch) |
| -wt-roundabout | agent/roundabout | e0c0d4b | 32 | yes | 12 | 9 | RUNNING |
| -wt-tjunction | agent/tjunction | e0c0d4b | 32 | yes | 5 | 8 | RUNNING |
| -wt-roadtool3 | agent/roadtool3 | e0c0d4b | 32 | yes | 13 | 8 | RUNNING |
| -wt-sliproad | agent/sliproad | e0c0d4b | 32 | yes | 1 | 5 | not in QUEUE Running list; queued after roundabout. Ask: running or premature? |
| -wt-grade-r9 | agent/grade-r9 | de26275 | 25 | no (base 0404204, 15 behind main) | 0 | 6 / 0.2 GB | RUNNING; will conflict with gradelayer on grading files |
| -wt-m2 | agent/unreal-m2 | 62368f3 | 15 | no (2 behind main) | 0 | 4 | handed in, auditor running |
| -wt-m3ui | agent/m3ui | 62368f3 | 14 | no | 0 | 4 | same tip as m2, no own commits; builder "next on the lane" |
| -wt-uelook | agent/uelook | 62368f3 | 14 | no | 0 | 4 | same tip as m2, no own commits; purpose not in QUEUE: STALE? |
| -wt-auditgate | agent/auditgate | c9c3616 | 6 | no (3 behind main) | 0 | 4 | handed in / own auditor pending |
| -wt-veg | agent/veg-kit-r2 | 12a4645 | 4 | no (41 behind) | 0 | 4 | kit stopped by owner decision (free assets): needs owner decision to archive |
| -wt-hooktest | none | none | n/a | n/a | n/a | 0 | orphan folder, 1 file, not a git worktree: remove |

Local branches with no worktree: none (21 agent/integ branches all attached). `origin` has only `claude/practical-ride-rrf8rx`; no agent branch is pushed. A stray ref named `origin` exists in branch listing (check `git show-ref origin`; may be a tag-like leftover).
Build dirs inside worktrees are small (most under 0.5 GB each, only grade 0.7 GB); the waste is mainly the main checkout. All worktree builds are from today.

### Main checkout root clutter
- Untracked: `mantissa_dl.log` (0 bytes), `unreal/CityPainter/SourceArt/Free/` (signs, walls), `SourceArt/Meshes/Free/` (Mantissa, PolyHaven). Two agents are writing these now: leave.
- 53 `build*` dirs (list: `build`, `build-agent-*` x34 incl. 5 `*.log` files, `build-bench/gcc/grading/grading-iso/junction/lights/m1-*/markings*/mesh/misc/msvc*/ref*/render/review3/roundabout/struct/ue/verify-*`); ignored by `build-*/` but not the `.log` files (those are covered by... check: `build-agent-rp*.log` are untracked-ignored? they did not show as `??`, so ignored by `build-*`-style pattern).
- Ignored heavy: `docs/reference` 317 MB (215 files), `unreal/CityPainter/{Binaries,Intermediate,Saved,DerivedDataCache}`, `prototypes/road-draw/{node_modules,progress}`, `tools/blender_kit/preview`, `.claude/state`, `Plugins/CityPainterSim` (leftover).

### Outside the repo (next to it or in home)
| Path | Size / files | What it is | Verdict |
|---|---|---|---|
| `Programming\roadworld-build-tool` | 416 MB / 719 | old build tree | owner OK, delete |
| `roadworld-b-tool2-gcc`, `-msvc` (+ `-msvc-build.log`) | 28 MB, 177 MB | road-tool-2 builds | delete after tool2 worktree gone |
| `roadworld-junc-gcc`, `-msvc` (+ `-gcc-build.log`, `-msvc-build.log`) | 31 MB, 176 MB | junction builds | delete |
| `C:\Users\bgbg0\cp_scratch` | 249 MB / 312 | composites, `pairs\` | move composites into `docs/reference/_compare/world-cover-r4/`, then delete (owner OK) |
| `C:\Users\bgbg0\livetest` | 237 KB / 69 | live-test scratch | owner OK, delete |
| `C:\Users\bgbg0\hk` | 354 KB / 5 | hook experiments (hooks now in `tools/hooks/`) | owner OK, delete |
| `Programming\roadworld-wt-hooktest` | 1 KB / 1 | orphan | delete |

### docs/progress (19 folders on main, 533 MB; more live only in worktrees)
- Naming: `2026-10-10-structures`, `-r2..-r5`; `2026-10-11-unreal-{second,third,fourth,fifth,m1,m1-playtest}`; `2026-10-10-unreal-first`; `2026-10-11-{gate-notices,hooks,junction-fixes,prototype-ui,structures-r6,test-world,vegetation-kit}`. Three schemes (date+topic, `-rN`, ordinals second/third/fourth/fifth).
- STATUS present: gate-notices, hooks, unreal-m1 (3 of 19). REVIEW/AUDIT present: structures, -r3, -r4, -r5, gate-notices, test-world, unreal-fourth/second/third, vegetation-kit (10). Neither: structures-r2, unreal-first, junction-fixes, prototype-ui, structures-r6, unreal-fifth, m1-playtest, hooks(status only).
- Only in worktrees (unmerged to main or reachable only via integ): junction-markings (+AUDIT.md), world-cover-r2 (+`REVIEW_R3.md`), -r4, structures-r7, -r8, road-tool, m2 and others. HANDOFF/README map these by hand.

### docs/*.md (19 files, 2255 lines) and docs/research (10 files, 1233 lines)
| File | Lines | Status |
|---|---|---|
| unreal-ui-parity 365, planning-log 254, performance-plan 204, unreal-photorealism 177, unreal-game-plan 170, HANDOFF 170, unreal-architecture 132, QUEUE 113, vegetation-and-surface-realism 98, road-graph-model 94, repo-health-review 87, sim-performance-baseline 77, v0-brief 66, junctions 65, road-marking-rules 58, README 56, windows-setup 36, AGENT_CONTRACT 20, ACCEPTANCE 13 | | |
| Stale by own dates: HANDOFF (header says 11 Oct 15:00, HEAD 269fa1e), README ("Rounds 1-49", "Last revised afternoon"), sim-performance-baseline ("dated, one busy-laptop run"), windows-setup/v0-brief (historical), repo-health-review (10 Oct, "current as a to-do source" but half-done, see (b)) | | |
| Overlap: `unreal-photorealism` / `research/ue58-feature-sweep` / `performance-plan` / `unreal-architecture` / `vegetation-and-surface-realism` / `research/vegetation-plan` (rendering stack and vegetation, three authors); `research/rules-audit` vs `rules-fix-mapping` vs `AGENT_CONTRACT` (rules history, keep as history); `research/mesh-terrain`, `pcg-city`, `pre-research`, `civil-engine`, `infinigen-preflight` are closed one-off verdicts | | |
| Not yet on main: `research/free-assets-vegetation.md`, `free-assets-roads-world.md`, `docs/reference/MATCHED.md` (being written now) | | |

## Plan coherence (answers brief item 3)
There is no single place. The milestone list M1-M8 is copied into three documents with different state columns:
- `unreal-game-plan.md` §C: scope and "verified by" (no state). Right place for scope; cannot hold state.
- `HANDOFF.md` §4: state as of 15:00 (M1 "playtest 9/36 failing"; M2 "no own commits"): wrong now.
- `QUEUE.md` "The plan": the only current one, with state (M2 under audit, M3 starting...), but it names engine work by agent, not by branch/commit, and "Running now" repeats branch info a third time. Two rows are inconsistent with git: grade-r9 says "started from agent/grading-r6 2aff581 + main" (git base is 0404204); junction row says "merge d611346" (that is the integ content, the branch tip is be59b50 = audit only).
Other contradictions: HANDOFF §12 "first steps" (finish M1 playtest, merge grade/junc/cover) is done; QUEUE critical path item 6 says "after item 6" (self-reference, should be item 5); QUEUE item 8 holds a 40-line spec that belongs in a task file; QUEUE "Small fixes" is a dumping ground that already contains three items the reorganiser will do. CLAUDE.md is correct as rules only but its Layout block omits `docs/progress/`, `docs/research/`, `.claude/`, `docs/reference/`.

Proposed owners: CLAUDE.md = rules only; AGENT_CONTRACT = agent rules; `unreal-game-plan.md` = scope, milestones, risks, no state; QUEUE.md = the plan table (milestone, state, branch, auditor owed) + running + next; HANDOFF.md = how to start a session and facts that rarely change (machine, build commands, owner standing quotes), no state tables; planning-log = decisions only.

## Branch flow (answers brief item 4)
Order, one at a time, clean builds both compilers after each (CLAUDE.md merge rule):
1. `integ/2026-10-11` (265bd1a, contains world-cover r4, junction markings d611346, grading r7+r8, road-tool-2, main). Blocker: QUEUE says integration hand-in and its independent clean build + suite on gcc and MSVC; main fast-forward only after that. The `-wt-integ` worktree shows tip e0c0d4b while the branch is 265bd1a: check `git -C ..\roadworld-wt-integ status` (a merge commit may be recent).
2. `agent/unreal-m2` 62368f3 (+ identical `m3ui`, `uelook`). Blocker: auditor AUDIT.md (running, holds Unreal lane); known failure: asphalt under terrain on slopes (fixed by item 3 below). Rebase onto new main (2 behind, plus integ).
3. `agent/auditgate` c9c3616: blocker: its own auditor; 3 behind main, touches `tools/hooks`.
4. `agent/grade-r9`: blocker: hand-in + auditor (section sheets vs height data); based on 0404204, will conflict with gradelayer (both grading); merge grade-r9 before gradelayer.
5. `agent/gradelayer`, `roundabout`, `tjunction`, `roadtool3`, `sliproad`: all on e0c0d4b; blockers: hand-in, auditor each; roundabout before tjunction before sliproad (same junction files, QUEUE item 4).
6. `agent/veg-kit-r2`: not a merge candidate: owner decides archive/delete (kit stopped).
Removable once item 1 is on main: `-wt-grade`, `-wt-cover`, `-wt-tool2`, `-wt-junc` (after copying its AUDIT.md/progress folder). Removable now: `-wt-tool`, `-wt-world`, `-wt-rules` (merged into main), `-wt-hooktest` (orphan). After item 2: `-wt-m3ui`/`-wt-uelook` if they have no own commits at that time.

## (d) Target organisation
```
roadworld\                       main checkout, always on main, never builds except ctest dir `build\`
  CLAUDE.md                      rules only (main session + pointer to AGENT_CONTRACT)
  docs\
    README.md                    index, one line per file, updated whenever a file is added
    PLAN.md  (= unreal-game-plan.md renamed; keep old name if links break)  scope M1-M8, never state
    QUEUE.md                     the ONLY state: milestone table (state + branch + auditor owed), running, next, waiting on owner
    HANDOFF.md                   cold-start facts only (<80 lines), no per-branch state
    planning-log.md              decisions only, append-only
    decisions\ architecture\     (optional) unreal-architecture, road-graph-model, junctions, performance-plan
    research\                    dated, one verdict each; rules-history files under research\history\
    progress\<yyyy-mm-dd>-<topic>-r<N>\   STATUS.md FAULTS.md COMPARE.md INDEX.md AUDIT.md; one scheme only
    reference\                   private, gitignored
    templates\
  tools\ tests\ sim_core\ unreal\ prototypes\ data\ third_party\
..\roadworld-wt\<name>\          ALL worktrees under one parent folder (new ones only; old ones are left where they are)
  <name>\build\                  ONE build dir per worktree, inside it, gitignored
..\roadworld-scratch\            agents' scratch (replaces cp_scratch, livetest, hk, roadworld-b-*, roadworld-junc-*)
```
Naming: worktree dir = `<branch-slug>`, branch = `agent/<slug>`, progress = `docs/progress/<date>-<slug>-r<N>`, scratch = `roadworld-scratch/<slug>/`. Max one `build` (plus `build-gcc` if needed) per worktree: briefs say so; no `build-agent-*`. Lifecycle: when a branch is merged to main and its auditor has written AUDIT.md, the main session (not the agent) runs `git worktree remove`, deletes `build*`, deletes the branch, and moves the progress folder into main; that step is a QUEUE line item, not an afterthought. A worktree with no commits and no queue row within one day is removed. Hand-off docs mention branches only inside QUEUE.

## (e) Work list for the reorganising agent
Rules: commit with `[skip ci]` and the attribution line, in the main checkout only; do not touch running agents' worktrees.

### SAFE NOW (touches nothing an agent is using)
1. `docs/README.md`: update to Rounds 1-50, list QUEUE, ACCEPTANCE, AGENT_CONTRACT, templates, `progress/2026-10-11-{gate-notices,hooks,unreal-fifth}`; note the three unmerged-progress locations; add the new target layout block from (d).
2. `CLAUDE.md` Layout block: add `docs/progress/`, `docs/research/`, `docs/reference/` (private), `.claude/`, `tools/hooks/`; keep it under 70 lines.
3. `docs/HANDOFF.md`: delete §4 table (replace by "see QUEUE.md The plan"), §7, §8, §12 and the header HEAD line; keep §1, §2 (re-verify counts), §3, §5, §6, §9, §10, §11. Target under 100 lines. Fix "up to Round 48".
4. `docs/QUEUE.md`: fix "after item 6" in critical path item 6; correct the grade-r9 base (0404204) and junction row (tip be59b50, content d611346 in integ); move item 8's 40-line list into `docs/tasks/roadtool3-gaps.md` (new folder `docs/tasks/`, referenced by the brief); remove from "Small fixes" the items this list completes; add a column "Branch (tip)" to the milestone table and one row each for `m3ui`, `uelook`, `sliproad`, `veg-kit-r2`, `auditgate` stating status or "STALE".
5. `docs/unreal-game-plan.md` §C: add a one-line note "Scope only. State is in QUEUE.md." and remove nothing else.
6. Move to `docs/research/history/`: `rules-audit-2026-10-11.md`, `rules-fix-mapping-2026-10-11.md`, `reference-claims-audit.md`; update README links. Move `docs/windows-setup.md`, `v0-brief.md`, `sim-performance-baseline.md` under `docs/history/` or mark with a first-line "HISTORICAL" banner (choose the banner: less link breakage).
7. `.gitattributes`: add `* text=auto eol=lf` as the first line (verify nothing else changes with `git diff --stat` after `git add --renormalize .` is NOT run; only the attribute line is committed).
8. Add to repo `.gitignore`: `*.log` at the root (`/mantissa_dl.log`, `/build-agent-*.log`) and `docs/reference/MATCHED.md` need no entry (already under ignored `docs/reference`). Do not remove `mantissa_dl.log` (an agent is writing it; 0 bytes).
9. Write `tools/worktree_status.ps1` (read-only): for each registered worktree print branch, tip, ahead/behind main, merged-into-main, dirty count, `build*` count and size, last commit age; and list `roadworld-*` / `cp_scratch` folders next to the repo. The main session runs it at session start (QUEUE line).
10. Edit `tools/new_agent_worktree.ps1` to create new worktrees under `..\roadworld-wt\<name>` and print the single build dir path; and add one line to AGENT_CONTRACT item 8: "one `build` dir inside your worktree, no others; scratch goes to `..\roadworld-scratch\<slug>`".
11. Add `docs/progress/README.md`: naming rule, required files, which folders have no STATUS/AUDIT (list from (c)), and where the unmerged ones live. Do not rename existing folders (links in planning-log).
12. Memory notes (outside the repo, `C:\Users\bgbg0\.claude\projects\...\memory\`): add `dont-rush-a-stop.md`, `think-about-parallelism.md` and `repo-organisation-rules.md` (worktree lifecycle, one plan, one queue, scratch location) plus MEMORY.md index lines. Flag in the report that this is outside the repo (the owner's memory dir; low risk).

### AFTER AGENTS FINISH
13. After `integ/2026-10-11` hands in and its independent audit passes (integration agent + auditor): fast-forward main to integ (main session only), then remove `-wt-grade`, `-wt-cover`, `-wt-tool2`, `-wt-junc` (first copy `be59b50` AUDIT.md folder `docs/progress/2026-10-11-junction-markings` into main; it is the only copy), `git branch -d` their branches after `merge-base --is-ancestor` into main confirms.
14. After step 13: `-wt-integ` removal and `git branch -d integ/2026-10-11`.
15. After `agent/unreal-m2` AUDIT.md exists and main has m2: remove `-wt-m2`; then `-wt-m3ui` and `-wt-uelook` only if still without own commits (check `git log main..branch`); otherwise rebase them first.
16. After `agent/auditgate` hand-in and audit: merge, remove `-wt-auditgate`.
17. After `grade-r9`, `gradelayer`, `roundabout`, `tjunction`, `roadtool3` each pass hand-in + audit and are merged: remove each worktree and branch; one row per removal in QUEUE.
18. After the two free-asset agents and the MATCHED.md agent finish: `docs/README.md` rows for `research/free-assets-*.md` and `reference/MATCHED.md`; commit `SourceArt/Free` metadata only after the owner approves what goes into LFS (see 23); delete empty `mantissa_dl.log`.
19. When the Unreal lane is idle (no editor or UBT running): delete `unreal/CityPainter/Plugins/CityPainterSim` (ignored leftover) and run `Saved/` and `DerivedDataCache` size check; do not delete `Intermediate` of the live plugin.
20. In the main checkout, once no agent builds there: delete the 38 `build*` dirs older than today (keep `build`, `build-gcc`, `build-msvc`, the 15 from today). Safe to do now only if `Get-Process` shows no cmake/ninja/msbuild in that folder.

### NEEDS THE OWNER (delete unmerged, outside the repo, or irreversible)
21. Delete outside-repo folders: `roadworld-build-tool`, `roadworld-b-tool2-*`, `roadworld-junc-*` and their `.log` files, `C:\Users\bgbg0\livetest`, `C:\Users\bgbg0\hk`, `roadworld-wt-hooktest`; and `cp_scratch` after its composites are copied to `docs/reference/_compare/world-cover-r4/` (owner OK already requested in QUEUE).
22. `agent/veg-kit-r2` (4 commits, not merged, kit stopped): owner decides keep as `archive/veg-kit-r2` tag then delete worktree, or keep. `agent/sliproad` and `agent/uelook` (no clear purpose): confirm running or drop.
23. LFS: `.git` 1.8 GB and 953 LFS objects. Owner checks the GitHub LFS quota before `SourceArt/Free` (and any more art) is committed; decide whether Free assets stay untracked (local only) or go to LFS.
24. `git worktree prune` / `git branch -D` for any branch not an ancestor of main or integ (veg-kit-r2, uelook if it has commits): never without the owner.
25. The five settled contradictions (fresh agent per round; ask split; 3 photos; parallel cap; stop rule) in rules-fix-mapping "Resolved, owner to confirm": owner says yes/no once, then the reorganiser deletes that section.
26. Repo-health code items 15-19 in (b) (split the god files, command-only RoadGraph mutators, test tags, untrack `x/` and generated `index.html`): scheduling is a plan decision (they conflict with every running engine branch); owner decides when, QUEUE gets one row "Cleanup" per item.
