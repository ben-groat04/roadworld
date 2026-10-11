# Rules fix: where every old rule went (11 Oct 2026)

C = `CLAUDE.md`, K = `docs/AGENT_CONTRACT.md` (contract, numbered), B = `docs/templates/BRIEF.md`, S = `docs/templates/STATUS.md`. Old line numbers are from `CLAUDE.md` before this change. "Audit #" is the rule number in `rules-audit-2026-10-11.md` section A.

## Old CLAUDE.md

| Old rule | New location |
|---|---|
| L3-4 source-of-truth pointers (planning log, architecture, README, v0 brief, markings) | C header, unchanged |
| L7-15 Layout | C Layout (tools line extended with the three scripts) |
| L18-21 Rules (Command, Id, doubles, driving side) | C Rules, unchanged |
| Gate 1 (1) photos named, >=3 per case | C gate 1 item 1; K1; B "Cases" |
| Gate 1 (2) owner's words verbatim | C gate 1 item 2; B "Owner's words"; `make_brief.mjs` refuses without a quote |
| Gate 1 (3) finish line = photo, self-audit passes | C gate 1 item 3; K (top line), K4, K5; B "Finish line" and "Self-audit" |
| Gate 1 (4) hand-in: side-by-side, same-camera before/after, fault list, skipped first | C gate 1 item 4; K3, K4, K6, K7; B "Hand in"; S; `check_handin.mjs` |
| Gate 1 (5) background, worktree, download line | C gate 1 item 6; K2, K8, K12; B "Cases" download line |
| Gate 2 (1) read STATUS for skipped/refused/failed | C gate 2 item 2 |
| Gate 2 (2) side-by-side files exist, else back unread | C gate 2 item 1 (`check_handin.mjs`) |
| Gate 2 (3) open headline images, "what would never be built?" | C gate 2 item 3; `docs/ACCEPTANCE.md` |
| Gate 2 (4) same-camera movement, else change method | C gate 2 item 4 |
| Gate 2 (5) commit [skip ci], push in background, pictures as files with own fault list | C gate 2 item 5 |
| L28 main session plans; all coding to subagents; Sonnet/Haiku | C Main session item 1 |
| L29 ask often on design/UX | C Decisions item 1 (RESOLVED: ask on product, scope, tool feel; see below) |
| L30 never end a turn uncommitted | C gate 2 item 5 |
| L31 default to parallel, split by file/function, tight scope, exact names | C Main session items 2-3 ("continue existing agents" part RESOLVED: see below) |
| L32 `scene.mjs --check` | C Main session (verify bullet) |
| L33 and L44 send pictures (one rule, merged) | C gate 2 item 5 |
| L34 cold-read Sonnet review; main session final check; confirms images | C Main session (review bullet); C gate 2 item 3 |
| L35 read HANDOFF first | C Working rules header |
| L36 reference claims need opened references; <2 says so | C "For every agent" item 1; K1; Main session review bullet; `docs/README.md` "Reference claims" kept |
| L37 Unreal single lane; prefer live editor via MCP | C Main session item 3; K9 |
| L38 worktree per agent; clean builds; merge one at a time; both compilers; trust only clean merged build | C "For every agent" worktree bullet; C Main session merge bullet; K8 |
| L39 usage limit: 3-4 agents; finish before more; resume; stop re-sent reports; commit WIP | C Main session items 2, 3, 4 (resume part RESOLVED) |
| L40 background; `[skip ci]` until CI asked; "last CI-verified commit 1ae80b0" | C Main session items 2 and last; K12. The commit hash is history: kept in `HANDOFF.md` section 3 ("Last CI-verified commit is still 1ae80b0") |
| L41 research Unreal facts from source | C "For every agent" last bullet; K14 |
| L42 briefs: no unbounded absolutes; judge results; report NOT verified; render and cold-read | C gate 1 item 6 (absolutes); K4, K6 (judge, not verified); C gate 2 items 3-4 |
| L43 `git status -sb` | C gate 2 item 5 |
| L45 decision split | C Decisions item 1 |
| L46 product framing (game, world_cover, lighting columns, no hand-dressed) | C Decisions item 2 |
| L47 reference photos are the target; 3 per case; top-down; lane widths; same framing; layout apart from look; "joins up" tests | C "For every agent" item 1; K1, K4 |
| L48 downloads pre-approved, with the owner's quotes, paths, INDEX logging, reference-only | C "For every agent" download bullet (quotes kept verbatim); K2 (quotes kept verbatim); B refers to K2 |
| L48 reference images private, never committed or copied elsewhere | C download bullet; K3 (composites go in gitignored `docs/reference/_compare/`; `.gitignore` now ignores `docs/reference`) |
| L49 builders audit themselves (owner quote, passes, fault list, empty list suspect) | C "For every agent" item 2 (quote kept); K4; B; `check_handin.mjs` (FAULTS.md with 2 passes and Residual) |
| L50 nothing fails silently | C "For every agent" item 3; K6; S first heading; `check_handin.mjs`; C gate 2 item 2 |

## Rules found only in memory, chat or other docs (audit section A and "Rules that exist only in memory or chat")

| Rule (audit #) | New location |
|---|---|
| 17 prototype UI is the UI spec | C Decisions item 3; K13 |
| 18 owner never opens the editor; agents test | C "For every agent" worktree bullet; C Main session item 3; K10 |
| 19 mute video | C gate 1 item 6, "For every agent"; K11 |
| 20 Unreal structure first (priority #1) | C Decisions item 3 |
| 22 fresh agent per round from STATUS; resume only short follow-ups | C Main session item 2 (RESOLVED) |
| 23 prefer free tools/assets; flag cost | C Main session last bullet; K13 |
| 27 never kill other processes | C "For every agent"; K9 |
| 28 reviewers judge lighting columns (not ignore) | C Decisions item 2; `research/vegetation-plan.md` ignore list fixed |
| 29 do not rush a stop at the usage limit | C "For every agent" ("It's max 5x so don't rush a stop"); C Main session item 4 (the standard message); K8 |
| 30 think about parallelism, checkable | C gate 1 item 5; C Main session item 3; B "Parallelism note"; `make_brief.mjs` refuses without Launched and Waiting |
| 31 settle method and photos before building; change method if tuning stalls | C gate 2 item 4; `HANDOFF.md` section 10 (lessons) |
| 32 gore paving decision stale in Round 38 | Planning log Round 49 note (history not rewritten) |
| Memory: skip-ci, background slow commands | C Main session items 2 and last; K12 |
| Memory: reference-claims, build-from-photos, fetch-more-allowed, opus-final-check | as for L36, L47, L48, L34 above |
| Memory: decide-road-details-myself | C Decisions item 1 |
| Memory: agent-usage-discipline (worktrees, single lane, 3-4 agents) | C Main session items 2-3 |

Not dropped: nothing. Intentionally moved out of the rules file: "last CI-verified commit `1ae80b0`" (kept in HANDOFF section 3), dates and "(from the person)" labels (kept in the planning log).

## Resolved, owner to confirm

1. Fresh agent per round, started from the previous STATUS file; resume only for short follow-ups (under about 3 messages). Replaces the "continue existing agents" and "resume an agent with context" phrases.
2. Ask the owner about product direction, scope, how tools feel, purchases, downloads other than reference photos and irreversibles; decide road-engineering details yourself with a toggle. Replaces "ask the person often on design and UX".
3. Minimum 3 opened references per case; a score resting on fewer than 2 opened per view says so.
4. Run every independent item in parallel within 3-4 live agents and one Unreal editor process at a time; a held-back item needs a one-line reason in the brief.
5. Time-box in hours is gone; replaced by the stop rule (residual holds only unfixable items, or 3 self-audit passes).
