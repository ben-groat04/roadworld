# Why the working rules keep being broken: audit of the written instructions (11 Oct 2026)

Scope: text only. Read-only audit; this is the only file created. Times: the chat transcript uses UTC ("Z"); git commit times and file times on this machine are local (UTC+1), so a commit at 15:15 local is 14:15Z. All "brief #N" numbers are the order of Agent/SendMessage calls in the main session transcript (extracted to the scratch area; duplicates of #1-#129 appear again at #130-#206 and were ignored).

## Not read or not checked (read this first)

- The transcript was only mined for (a) every Agent prompt and SendMessage text (211 items; briefs #1-#129 and #207-#210 used, the rest are repeats or this audit), and (b) the owner's chat messages (390 text turns; the ones under 700 characters were listed). The agents' final reports (SubagentHandback texts) were NOT read; section D rests on the STATUS, REVIEW and REFERENCE_NOTES files only.
- Briefs scored in section C: the 8 visible-work briefs of 11 Oct (#90, #93, #113, #114, #118, #123, #126, #128) were read in full, plus the follow-up messages #94-#125 and #207-#210. Earlier visible-work briefs (#14, #17, #27, #28, #37, #40, #47, #49, #50, #61, #82, #88) were only scanned for key words and opening paragraphs. Pure research, housekeeping and review briefs were not scored.
- `docs/planning-log.md`: read Rounds 23-48 plus word searches; Rounds 1-22 not read. `docs/unreal-game-plan.md`, `unreal-architecture.md`, `unreal-ui-parity.md`, `unreal-photorealism.md`, `performance-plan.md`, `vegetation-and-surface-realism.md`, `junctions.md`, `road-graph-model.md`, `repo-health-review.md` and the `docs/research/*` files other than `reference-claims-audit.md` and `vegetation-plan.md` were searched for rule-like statements by word search, not read through. `unreal/tools/README.md` and `KNOWN_RISKS.md` were searched only. `tools/**` and `prototypes/**` READMEs were not read (no rule content expected; not checked).
- Old REVIEW files (`structures` r1, r3, r4, r5, `unreal-second/third/fourth`, `vegetation-kit`, `test-world`) were checked only for whether they list the reference files opened and a confidence line, not read for content. `REVIEW_R3.md` (cover) likewise.
- No image was opened. I did not verify that any claim in a STATUS file is true in the pictures.
- The reference INDEX files were only searched for rule text (there is none about downloads).
- I did not check whether Claude Code hooks, skills or settings exist for this project beyond confirming the repo has no `.claude/` folder. I did not check how the harness refreshes CLAUDE.md in the main session. Whether a Claude Code hook can block a subagent hand-in (section H item 9) is not verified.
- Whether the owner's other chat messages not matched by my word searches contain more rules: not established (178 short messages listed; keyword filtered for the ones quoted here).

---

## Summary for a non-programmer (two minutes)

**What happened.** You have given the same rules many times. They are written down. They still get broken. Having read the instructions, the briefs sent to the agents, and the agents' notes, my finding is: **the rules are not hidden or missing. They are scattered, they arrive late, and the person who has to apply them (the main session) does not have a routine that makes it stop and check.** Most of the blame is on how briefs are written and how reports are accepted, and some is on the documents.

**Top five causes**

1. **Agents often never see the newest rules.** Each long-running agent works in its own copy of the project. The copies for earthworks, junction markings and world cover were made before most of today's rules were written, and they still do not contain them. I checked: those three copies of `CLAUDE.md` have no download approval, no "nothing fails silently", no "main session opens the images" and no self-audit rule. One brief (#123) told the earthworks agent to read "the last lines of CLAUDE.md" for the download approval; those lines do not exist in that agent's copy. The agent that refused to download photos did so because the approval reached it only second-hand. The same gap still exists for every rule that is only passed on by chat message.
2. **Briefs make the list of faults the finish line, and give a time limit.** Your rule (since 08:59Z) is that the photo is the target. But the earthworks round 8 brief (#123, 13:40Z) says "these are the whole task", and every brief ends with "time-box about 2-3 hours; report the rest as not done". That is permission to stop with items undone, and agents used it: wall ends were "NOT changed" in two successive earthworks rounds; three junction items were "NOT done" after two sessions. This is why progress looks slow and why "the agents are lazy" feels true. It is the briefs, not laziness.
3. **Rules are added after each failure as one more line, in five places.** Each rule is stated in `CLAUDE.md`, `HANDOFF.md`, the planning log, the memory notes and the briefs, with slightly different wording, and two pairs contradict. Nothing says which copy wins, and the main session's own routine did not change until the "Two gates" were added at 14:15Z, after the failures.
4. **Nothing checks anything.** There is no template for a brief, no template for a report and no check that a hand-in has the required files. Every rule is checked only by the same session that wants to move on. The result: a "fixed" claim was passed on without opening the picture (roundabout, 13:17Z; you had to say "look yourself").
5. **Some of your rules exist only in chat or in memory.** "Don't rush a stop", "think about parallelism" (you asked four times), "mute videos", "the prototype's UI is the spec" and "I will never open the editor" are not in `CLAUDE.md`, so no agent receives them unless a brief repeats them. "Don't rush a stop" is written nowhere at all. At 09:43Z the main session sent every agent a "stop now" message one minute after you said not to rush a stop.

**Top five fixes** (details in section H)

1. Make one short **agent contract** file and have a small script paste it, unchanged, into the top of every brief, so no agent depends on the age of its project copy. (Needs your OK on the wording only.)
2. Make **every brief come from a template** whose finish line is the photo, with the owner's words quoted, a required self-audit with fault list, named hand-in files and "skipped items first". Remove "these are the whole task" and replace the time-box with "stop when the residual list is empty or you can name why each item cannot be fixed".
3. Add a **hand-in check script** the main session runs before reading any report: if the files are missing, the report is sent back unread.
4. **Clean up the rule text**: one source (CLAUDE.md, about 30 short lines), HANDOFF and the planning log point to it instead of copying it, resolve the two contradictions, add the missing rules (stop at the limit, parallelism, mute video, editor, prototype UI).
5. Create agent worktrees with a **script** that starts from the current main branch and links the private reference folder, so no agent runs on an old rule file or cannot find the photos.

---

# A. Rule inventory

Legend for places: C = `CLAUDE.md` (line), H = `docs/HANDOFF.md` (line), P = `docs/planning-log.md` (round), R = `docs/README.md`, M = memory note, B = only in briefs/messages, "-" = absent. Line numbers are those on disk at the time of the audit (C includes the "Two gates" paragraph added at 15:15 local).

| # | Rule | Where stated | Agree? | Notes |
|---|---|---|---|---|
| 1 | Reference photos are the target, opened BEFORE building, at least 3 per case, top-down for layout, measure in lane widths, name files, render at same framing, compare item by item, score layout apart from look | C47, Gate 1(1); H12; P42; M build-from-reference-photos; briefs #90,#93,#113,#118,#123,#128 | Mostly. Minimum is 3 in C47/H12/P42/M but 2 in M reference-claims and in C36/R54 ("fewer than two ... must say so") | Not contradictory (3 to work, <2 must be flagged) but two numbers in play. Owner said it at 07:25Z, 08:54Z, 08:58Z, 09:45Z, 13:23Z, 13:48Z |
| 2 | Reference claims must name the files actually opened; INDEX text is not evidence | C36; R51-55; P39; M reference-claims-need-opened-photos; `research/reference-claims-audit.md` | Agree except threshold 2 vs 3 (above) | Five places |
| 3 | Reference-photo downloads pre-approved (reference images only) | C48; H13; P42; M fetch-more-reference-images-allowed; briefs #94,#95,#98,#107,#113,#120-#123,#128 | Agree | Not in any reference INDEX, not in the old worktree copies of C (see B) |
| 4 | Nothing fails silently: skipped/refused/failed goes in FIRST lines of report and STATUS.md; main session reads STATUS | C50; H14, H142; P42; M (inside fetch-more note); briefs #120-#123,#126-#129 | Agree | Added 13:25Z |
| 5 | Main session opens headline images itself when an agent says good/fixed/done and asks "what would never be built?" | C34, Gate 2(3); H17, H142; P42; M opus-final-check-on-good-claims | Agree. C34 also says reviews go to agents and main "does not audit work in progress" | Gate 2(3) and C34 are consistent; the stale "main session never audits screenshots" line was removed in 9cb0b5d (git history shows it only in old commits) |
| 6 | Builders audit themselves: finish line is the photo, side-by-side, own fault list, fix, up to ~3 passes, hand in before/after pairs and residual faults | C49, Gate 1(3)(4); M agents-self-audit-before-review; briefs #207-#210 only | **H and P do not mention it** (HANDOFF written 15:00 local, rule 15:13) | Three places; two of them not seen by agents |
| 7 | Unreal is a single lane | C37; H138; P41,P44; M agent-usage-discipline | Agree | |
| 8 | One worktree and branch per code agent; clean builds; merge one at a time | C38; H139, H118; M | Agree | HANDOFF branch map stale (HEAD) |
| 9 | Watch usage limit: max 3-4 agents; finish before starting more; commit WIP | C39; H144; M | Agree with each other; **conflicts with rule 21** | |
| 10 | Agents always run in the background; every push has `[skip ci]` | C40; Gate 1(5); H145; M skip-ci, background-slow-commands | Agree | |
| 11 | Research Unreal/plugin/tool facts with agents, never from memory | C41; H146; M | Agree | |
| 12 | Briefs: no unbounded absolutes; require "what you did NOT verify"; render and cold-read every geometry/look change | C42; H147; P39 | Agree | |
| 13 | Check commits landed with `git status -sb` | C43; H147; M | Agree | |
| 14 | Send the person pictures as files | C33 **and** C44 (identical rule twice); H148; Gate 2(5) | Agree (duplicate line) | |
| 15 | Decision split: road-engineering details are the assistant's; product, scope, purchases, downloads, irreversibles are the person's | C45; H20; M decide-road-details-myself | **C29 says "Ask the person for input often on design and UX decisions"** | Mild contradiction: C29 asks often, M/C45 say do not ask about road details. Reconciled only in memory |
| 16 | Product framing: it is a game; everything in the picture comes from world_cover plus road-generated content; nothing hand-dressed | C46; H9-11; P35; M road-only-in-open-world | Agree | |
| 17 | Prototype UI is the spec for the game UI | H15; P43; M prototype-ui-is-the-target-ui; brief #126 | **Absent from C** | Agents see it only if the brief says it |
| 18 | The owner never opens Unreal or playtests; agents drive and test it; a milestone is not done until an agent has exercised it | H16; P44; M user-wont-open-editor-agents-test; brief #114 | **Absent from C** | |
| 19 | Browser video must be muted | H18; P48; M browser-video-must-be-muted; messages #112,#115 (after the fact) | **Absent from C** | Agents were not told in the brief; owner heard it |
| 20 | Unreal structure first (priority #1) | H19; P34; M unreal-phase-structure-first; C header link only | Agree; C has only a link | |
| 21 | Default to parallel agents split by file/function; continue existing agents via messages | C31, C39 ("resume an agent with context") | **Contradicts rule 22 and rule 9 in spirit** | Owner asked about parallelism at 22:46Z (10 Oct), 07:19Z, 12:43Z, 13:52Z |
| 22 | Fresh agent per round starting from a STATUS file; resume only for short follow-ups; fewer, larger tasks | H141; P48; M agent-usage-discipline | **Directly opposite to C31/C39** | Real contradiction (see below) |
| 23 | Prefer free tools/assets; flag any cost | M prefer-free-tools-and-assets; H149 (one clause); P32,P37 | Not in C | Reaches agents only through briefs (#70-#72 had it) |
| 24 | Never end a turn with uncommitted changes: commit and push | C30; Gate 2(5) | Agree | The git status at the start of this session shows 19 deleted PNGs and 3 modified files uncommitted |
| 25 | All coding delegated to subagents; Sonnet default, Haiku for trivial edits | C28; P27 | Agree | |
| 26 | Verify with `scene.mjs --check` rather than screenshot loops | C32 | Tension with C42/C49 (render and loop) | Different scopes (invariants vs realism) but unstated |
| 27 | Never kill compiler processes that are not yours | H138; P48; briefs #113,#114,#123,#126,#127 | Absent from C | |
| 28 | Reviewers ignore buildings, fences, signs, vehicles, people; lighting columns ARE judged | `research/vegetation-plan.md:43`; H11; P35 | **`vegetation-plan.md:43` still lists lighting columns under "Ignore"**; H11 says it overrides | Stale source text; a reviewer who reads the plan gets the old list |
| 29 | Don't rush a stop at the usage limit: wrap up cleanly, finish properly | **Nowhere** (chat only, 09:42Z and 09:43Z; compaction summary) | - | See G |
| 30 | Think about parallelism | **Nowhere** except generic C31 | - | Four owner prompts |
| 31 | Settle method and photos before building; if tuning stalls after one round change method; downscaled images/cheaper model for reviews | H140, H141, H143; M agent-usage-discipline | C has only Gate 2(4) "change method" | |
| 32 | Slip gores: paved (P38) versus no paving in the gore (brief #90, H83) | P38 vs H83/#90 | **Stale decision in P38** | Round 38 text was never superseded in the log |

## Contradictions and stale statements, in order of damage

1. **Resume vs fresh agent**: C31 ("continue existing agents via messages instead of spawning new ones") and C39 ("resume an agent with context rather than spawning a new one") against H141, P48 and M ("fresh agent per round; resume only for short follow-ups"). The main session followed the HANDOFF version (briefs #113, #118, #123 are "fresh start") and the CLAUDE.md version (#98, #107, #119 messages to old agents). Both are "rule-compliant", so neither can be called a breach.
2. **Parallel vs cap**: C31 "default to parallel agents" against C39 "at most 3-4 at once, finish before starting more" and C37 "Unreal is a single lane". There is no sentence saying what to do when work could be split but the cap or the lane applies (answer implied: use sim_core worktrees in parallel while Unreal is the lane; H138 says it, C does not).
3. **Private photos vs "side-by-side image files"**: C48 says reference images are "never committed or copied elsewhere". Gate 1(4) and Gate 2(2) require side-by-side image files as the hand-in. A composite contains the private photo, and `docs/progress/` is committed. Precedent that works: `docs/reference/gsv/compare/` (composites kept inside the ignored folder, mentioned in the `unreal-second` review and brief #61). The Gates do not say where composites go, so a compliant agent would either break the privacy rule or skip the files.
4. **Ask often vs decide yourself**: C29 against C45/M decide-road-details-myself.
5. **Photo minimum 2 vs 3** (above).
6. **HANDOFF is out of date against CLAUDE.md**: H3 says HEAD `269fa1e`; HEAD is `dbf08f9`. H does not contain the self-audit rule or the Two gates (written 15:00 local; rules at 15:13/15:15). H142 says only "agents open their own renders before reporting", a weaker version of C49.
7. `research/vegetation-plan.md:43` ignore list (above). P38 gore paving (above).
8. C40 holds "last CI-verified commit `1ae80b0`" and C46 "(11 Oct)": history inside the rules file (see F).

## Rules that exist only in memory or chat (so never reach agents)

- Absent from CLAUDE.md, present in memory and HANDOFF: prototype UI is the spec (17), owner never opens the editor (18), mute video (19), structure first (20), free-first (23), fresh-agent-per-round (22), efficiency lessons (31).
- Present nowhere in the repo: don't rush a stop (29), think about parallelism (30).
- Present only in compaction summaries and briefs: never kill other agents' compiler processes (27 is in HANDOFF but not CLAUDE.md).

---

# B. Reach: who has to act, and do they get it where it matters?

What an agent receives: CLAUDE.md of **its own worktree** (the file as of the commit its branch was created from or last merged) plus its brief. HANDOFF is read only if the brief says so. Memory notes are read by the main session only.

I compared the CLAUDE.md in each worktree with the main checkout:

| Worktree / branch | Created from (local time) | Has downloads approval | Nothing fails silently | Main session opens images | "Reference photos are the target" | Self-audit | Two gates |
|---|---|---|---|---|---|---|---|
| `-wt-junc` (junction markings, 399d042) | before 09:56 | no | no | no | **no** | no | no |
| `-wt-grade` (earthworks, cfd0776) | before 09:41 | no | no | no | yes | no | no |
| `-wt-cover` (world cover, ac24efa) | before 10:04 | no | no | no | yes | no | no |
| `-wt-m2`, `-wt-tool2`, `-wt-veg` (269fa1e, 14:52) | 14:52 | yes | yes | yes | yes | no | no |
| main checkout | now | yes | yes | yes | yes | yes | yes |

(Checked by word search of each worktree's CLAUDE.md. None of the three older branches has merged a commit at or after f7a0b01: `git merge-base --is-ancestor f7a0b01 HEAD` fails in all three.)

Also: `docs/reference/` is gitignored, so **no worktree contains the photos**. CLAUDE.md and several briefs (#90, #93) say "References are in docs/reference/"; inside a worktree that path does not exist. Later briefs (#113, #118, #123, #128) give the main-checkout absolute path, which works. New reference INDEX entries are written into the main checkout by agents working in other trees.

Per rule and actor:

| Rule | Main session when briefing | Main session when accepting | Builder | Reviewer | Does the actor get it where/when it matters? |
|---|---|---|---|---|---|
| 1 photos first | Gate 1(1) (added 14:15Z) | - | C47 (stale in `-wt-junc`) and brief | brief (#60 etc.) | Builders: only via the brief. Main: gate came after the failures |
| 3 downloads | must point at it | - | C48 (absent in 3 worktrees) | - | **No for 3 of 6 worktrees.** Brief #123 pointed at "last lines of CLAUDE.md" that the grade agent's copy does not have. Brief #90 (junction) said nothing about downloads; #95 relayed "The person has approved ..." and the agent declined it as second-hand |
| 4 nothing silent | must require in brief | Gate 2(1): read STATUS | brief + C50 (absent in 3) | - | Agents after 13:25Z got it in the message (#120-#122) and in new briefs. It worked: the four STATUS files after the rule all open with a skipped list |
| 5 main opens images | - | Gate 2(3) | - | - | The only rule whose actor is the main session alone. Added 13:21Z (C), 14:15Z (Gate). Before that the memory note existed only as an intention |
| 6 self-audit | Gate 1(3)(4) | Gate 2(4) | brief only (#207-#210, relays) | n/a | **Builders got it only as a chat relay, the exact route that failed for downloads.** Not in their CLAUDE.md copies |
| 9 usage limit / 29 don't rush a stop | - | - | main session only | - | Main session acted against the owner's words at 09:43Z (see D) |
| 19 mute video | brief must carry | - | not in C | - | Not in the research briefs; owner heard it; fixed by message #115 |
| 17/18 prototype UI, editor | brief must carry | - | not in C | - | Carried in #114 and #126 by hand |

Conclusion for B: **the only reliable route to an agent is the brief text.** The briefs were mostly good (see C), but the main session assembles them from its own memory of the rules each time, and for the newest rules the project file the agent reads is out of date.

---

# C. Briefs against the rules in force at the time

Rules in force are dated by the commit that first wrote them (converted to UTC): reference-first (C47) 08:59Z; main-session-confirms (C34) 13:21Z; downloads + nothing-fails-silently (C48, C50) 13:25Z; self-audit (C49) 14:13Z; Two gates 14:15Z. Research, housekeeping and review briefs were not scored.

Visible-work briefs of 11 Oct (read in full). Y = yes, P = partly, N = no, n/a = rule not yet in force or not relevant.

| Brief | Time (Z) | Refs named, >=3 per case | Owner quoted verbatim | Finish line = the photo | Self-audit with own fault list | Hand-in files specified | Download approval pointed at | Skipped items first | Time-box / stop permission |
|---|---|---|---|---|---|---|---|---|---|
| #90 junction markings | 08:55 | Y (cases a-e, >=3 each, preferred top-down) | N (paraphrase "found five faults in a minute") | P ("compare", marking-by-marking checklist, but items 1-6 are the work) | N (checklist of the brief's items; "honest about every no") | P (INDEX pairing table, renders A-L; no side-by-side file, no own fault list) | n/a (approval given 09:15Z) | N (report asks "what you did NOT do" last) | "about 2.5 hours; ... report the rest as not done" |
| #93 world cover r2 | 09:03 | Y (>=8 aerials, names) | N | P (match maps beside photos) | P (iterate at most 3, checklist) | P (INDEX pairing, previews) | n/a | N (last) | "about 2 hours" |
| #113 earthworks r7 | 12:20 | Y (>=10, named, + fetch) | P ("angular and not smooth" quoted) | P (checklist per scenario) | N | P (INDEX pairing) | P (says owner approved fetching; no pointer) | N (last) | "about 3 hours" |
| #114 Unreal playtest | 12:23 | n/a (behaviour test) | N ("owner will not open the editor" paraphrased) | n/a | n/a | Y (INDEX table with image per control) | n/a | N | "about 3 hours" |
| #118 world cover r4 | 12:41 | Y (>=10 of 46-59) | N ("owner's rule" paraphrased) | P ("COMPARE HONESTLY", checklist, "would a person still say Voronoi") | P (3 look iterations) | P (INDEX_R4 pairing) | P (may fetch 8 more; no approval statement) | N | "about 3 hours" |
| #123 earthworks r8 | 13:40 | Y (named 04, 05, 46, >=4 more) | P (quotes only in message #124) | **N: "these are the whole task"** | N | P (INDEX pairing; "open every render yourself") | P: points at "the last lines of CLAUDE.md", which the grade worktree's copy does not have | **Y** (report "skipped/not-done FIRST") | "about 3 hours" |
| #126 Unreal M2 code | 13:52 | n/a (spec = prototype) | N | n/a | n/a | Y (STATUS with sections) | n/a | **Y** | "about 3 hours" |
| #128 vegetation kit r2 | 13:53 | Y (>=12, names) | N | P (render beside photo; "five-second tells" list) | **P+** (3 look iterations; writes what a person would spot as fake; closest to the later rule) | P (INDEX pairing, round-1 comparison) | **Y** ("pre-approved by the owner", C note; the veg worktree's CLAUDE.md does contain it) | **Y** | "about 3 hours" |

Follow-up messages: #94, #95, #98, #107 and #119 are good on content. #119 and #124 quote the owner verbatim and list fault points. #120-#122 quote the download approval verbatim and point at CLAUDE.md. #99-#102 (stop at 95%) contradicted the owner (see D).

**Miss counts (visible-work briefs #90, #93, #113, #114, #118, #123, #126, #128 unless marked n/a; counted only where the rule applies and was in force):**

| Rule | Applies / Misses | Comment |
|---|---|---|
| Reference files named, >=3 per case | 6 apply / 0 miss | Strongest area, because rule 1 was written first |
| Owner quoted verbatim | 8 apply / 6 miss (only #113 and #123 quote a fragment; verbatim blocks only in follow-ups #107, #119, #120-#124) | The memory note and H141 both say "pass the owner's words verbatim" |
| Finish line = the photo, not the item list | 6 apply (visible work) / 6 miss or partial (0 explicit; #123 says the opposite) | Rule 1 (08:59Z) implies it; wording "finish line" arrived 14:13Z |
| Self-audit with own fault list | 6 apply / 6 miss (rule in force only from 14:13Z, so these are history; #128 is nearest) | No brief before 14:13Z asked for faults beyond the brief's own items |
| Hand-in artefacts: side-by-side image files | 6 apply / 6 miss | None ever asked for a composite file; INDEX "pairing tables" are text. All unambiguous before the Gate existed |
| Same-camera before/after | 6 apply / 2 miss (#118, #128 only against previous round) | #90, #93, #113, #123 reuse fixed cameras |
| Download approval pointed at | 3 apply after 13:25Z (#123, #128; #126 n/a) / 1 effective miss (#123: pointer to lines the agent's copy lacks) | The approval itself was correct and verbatim in #120-#122 |
| Skipped items first | 5 briefs after 13:25Z (#123, #126, #127, #128, #129) / 0 miss | Worked once written |
| Background, own worktree | all / 0 miss after 20:46Z on 10 Oct (earlier briefs lacked `run_in_background`; owner: "agents should be backgrounded, why are you not answering me") | |

Pattern: every miss is a miss of a rule that was **added after the brief was written** or **stated in a form the main session had to remember**. The briefs that were checked against a fresh list of items (skipped-first, background, worktree) are all compliant. This points at a missing checklist at briefing time, which the Two gates now provide, but only as text.

Specific brief faults beyond the rules:

- #123 "these are the whole task" and "(the coordinator) found three things still wrong" is the clearest contradiction of the owner's wording ("finish line is the photo").
- Every brief carries a time-box and a word cap on the report (250-450 words). A report that must fit in 400 words and a task that ends at "about 3 hours" encourages summarising what is done and listing what is not.
- Briefs ask for "an honest checklist" of the brief's own items. That is a self-score, not a fault hunt. The owner's complaint is that the second layer of faults is found by the reviewer or the owner instead.
- Briefs #90 and #93 told agents the references were in `docs/reference/`, which does not exist in their worktrees.
- Commit trailers in briefs say "Claude Opus 5.5"; the active attribution for this session is Sonnet 5.5. Minor, flagged for tidiness only.

---

# D. Reports against the rules

Files read: `unreal-m1/STATUS.md`, `structures-r7/STATUS.md`, `junction-markings/STATUS.md`, `world-cover-r2/STATUS.md`, `world-cover-r4/STATUS.md`, `unreal-m2/STATUS.md`, and for the REVIEW files only the "opened" and confidence lines.

| Report (time written, local) | Skipped/not-done first? | References opened and named? | Own fault list? | Side-by-side images listed? | "Fixed/good/done" claim with a named image? |
|---|---|---|---|---|---|
| `unreal-m1/STATUS.md` (13:21, before rule) | No: ends "M1 complete. Open: ground material grey, void ..., PIE not tested by a human" | n/a | No | No (INDEX.md exists) | "M1 complete": later playtest run 1 had 9 of 36 checks failing (HANDOFF §2) |
| `structures-r7/STATUS.md` (14:34) | **Yes** (SKIPPED / NOT DONE first) | Yes (street/38-51 fetched and opened) | No (before rule) | No (INDEX pairs by name) | Smoothing "no crease visible" is carried by `s5_cutting_crest_profile.png` in INDEX.md; wall ends: NOT changed; adjacent_levels still waived |
| `junction-markings/STATUS.md` (15:12) | **Yes** (FIRST LINES block) | Yes (10+ files named in REFERENCE_NOTES) | No (rule arrived 14:13Z and the file is dated just before) | No | No "good" claims; honest about "renders not framed per reference photo", measurements "eyeballed +/-20%", items 3, 4, 5 not done |
| `world-cover-r2/STATUS.md` (13:33, before rule) | No: "Items 1-5 done" with a "Left:" line | Yes (14 aerials opened) | No | No | "Items 1-5 done" with no image named; the later cold read scored the result fields 6, boundaries 4, woodland 5 |
| `world-cover-r4/STATUS.md` (14:26) | **Yes** | Yes (11 of 14 named) | No (rule not yet in effect) | No | "Done so far" without images yet |
| `unreal-m2/STATUS.md` (15:18) | **Yes**, long list incl. "NOTHING WAS RUN" | Engine headers cited | No | No | None |
| Reviews (10 files) | n/a | Newer ones list opened files with counts (8, 9, 11, 15) and a confidence line; `structures-r3` admits "by INDEX text only, the images were not opened at the time" | n/a | n/a | n/a |

What these say:

- **The "skipped first" and "opened references named" rules are followed once written.** Every STATUS after 13:25Z leads with what was not done. Reviews since the audit of 10-11 Oct list the opened files.
- **No builder file contains its own fault list or a composite image.** Not surprising, since the rule arrived at 14:13Z and the files are dated before or just after. This is the gap the Two gates were written to close, but nothing asks for the files by name.
- **Honest reports produce visibly slow rounds.** Wall ends were NOT changed in r7 (brief #98 item 3, #113 item 4), the main session re-briefed them as item 1 of round 8. Junction items 3, 4, 5 are NOT done after two sessions, and the dark slot fix is "not confirmed in Unreal". The reports are doing their job; the briefs and the time-boxes give no consequence for not doing the work, so the owner sees "lazy".
- **The key failure of this day was not in a report but in acceptance.** At 13:17Z the owner saw the junction agent's roundabout preview and asked "did you not attempt to fix the roundabout arms then". The STATUS said the hatching "ends on boundary strokes" with a "short taper"; this was relayed without opening the image. The main session then looked and sent #119 with seven visual faults the picture plainly showed (no island, inside-out painted box, ladder hatching with a hollow centre, no give-way line). The owner then asked the main session to confirm images itself (13:20Z). The image-opening rule was written at 13:21Z, i.e. after the failure. The owner repeated "i don't like how i have to keep pointing this stuff out to you" later the same day (user message, line numbering in transcript text: 3773).
- **A "stop now" message overrode an owner instruction.** The owner wrote at 09:42Z "ensure things will wrap up cleanly ... don't rush a stop". At 09:43Z the main session sent #99-#102: "Do not start anything new ... report under 150/120 words". At 09:43:57Z the owner: "I said don't rush a stop". Corrections #103-#106 followed at 09:44Z. The system also announced a usage-limit grace at 10:01Z. Main-session behaviour, not a document fault, but no document states the rule.
- **Progress claims without a named image**: the `world-cover-r2` and `unreal-m1` STATUS files claim completion with no image named in the file itself. In both cases an INDEX.md exists beside them, so the claim is traceable but not named at the claim.

---

# E. Checkability

Mechanically checkable (a file must exist, a line must be present) vs intention only.

| Rule | Checkable today? | Proposed checkable form |
|---|---|---|
| 4 Skipped items first | Partly: STATUS.md first heading | Required first heading `## Skipped / not done / not verified` in STATUS.md (script checks it is the first heading and is not empty or "none" without a sentence) |
| 2 / 1 Reference files opened | Partly: text in REFERENCE_NOTES | `REFERENCE_NOTES.md` must contain a line per case: `Case: <name> | Opened: <file>, <file>, <file> | No photo covers: <...>`; script counts distinct files >=3 per case and checks each exists in the main reference folder (path given); fails if a "No photo" case has no fetch attempt noted |
| 6 Self-audit | No (intention) | `FAULTS.md` in the progress folder with sections `Pass 1`, `Pass 2` ... each listing faults and the fix; and `Residual` section; script requires >=1 pass, >=3 faults in pass 1 not copied from the brief (can only check count), and a non-empty residual or the explicit line `Residual: none; reason: ...` which the main session reads with suspicion. Plus an image manifest below |
| 6 Side-by-side | No | `COMPARE.md` manifest table: `case | render file | reference file | composite file | before file (same camera)`. Composites live in `docs/reference/_compare/<round>/` (gitignored, private) so rule 3 is kept; the script checks every listed file exists and is newer than the last source commit; image count equals case count |
| 5 Main session opens images | No (intention) | A line in the main session's reply is not checkable by a script, but a log can be: the main session writes `docs/progress/<round>/ACCEPTANCE.md` listing the images it opened (path), what it saw ("would never be built": 3 items or "none, because ..."), and the verdict, before sending anything to the owner. Reviewable by the owner |
| 3 Downloads allowed | Yes (pointer present in the brief) | Template line that quotes the owner's two sentences verbatim and the file path; script on the brief text checks that the quote is present |
| Quote owner verbatim | Yes | Template field `Owner's words (verbatim):` must be non-empty and in quotation marks for visible work |
| Background, worktree, skip-ci | Partly | Brief template boilerplate; script `git log -1` of the agent's branch for `[skip ci]` on pushes |
| 9 Max 3-4 agents | Yes | Main session keeps `docs/progress/AGENTS.md` (or reads Agent notifications) table: running agents, worktree, lane. Count is checkable by eye |
| 21 Parallelism | No (intention "think about parallelism") | Checkable form: **before launching one agent, the brief template asks "list independent work items; for each one not launched now, give the blocking reason (lane, shared file, usage limit)".** Written into the briefing gate as one line the main session must produce; the owner can read it |
| 29 Don't rush a stop | No | Rule text: "At 90% usage: send the one standard message (below) to each agent: finish the current verified step, commit, update STATUS.md, then continue; never send 'do not start anything new' or word limits unless the owner says so." A fixed text in the template makes the failure visible |
| Mute video | Yes | One boilerplate sentence in every brief that may open pages; script greps the brief for `muted` |
| Kill no foreign processes | Yes | Boilerplate sentence in build/Unreal briefs |
| 24 Commit at end of turn | Yes | `git status -sb` clean line in Gate 2(5); the harness `Stop` hook could print dirty status (not verified) |
| 26 `scene.mjs --check` | Yes | already a command |

Stated well today: skipped-first (heading), opened-files (partly), `[skip ci]`, git status. Stated as intentions: self-audit, "what would never be built", parallelism, don't rush a stop, quote verbatim, "think about the reviewer".

---

# F. Structure of CLAUDE.md

- Length: 50 lines, 1308 words. The "Working style" section is 24 bullets, about 900 words, in no priority order (new rules were appended at the bottom, so the two most-violated rules, 48-50, are the last three lines).
- Overlap and duplicates within the file: lines 33 and 44 are the same rule (send pictures) written twice; reference photos appear at C36, C47, C48, Gate 1(1); main-session image check at C34, Gate 2(3); background/push at C40, Gate 1(5), Gate 2(5); agent count at C31 and C39; resume at C31 and C39 against HANDOFF.
- History in a rules file: "last CI-verified commit `1ae80b0`" (C40), "(11 Oct)" dates, "(from the person)" labels, a worked example "as many tiers as needed" in C42, and quotes with dates in C48 and C49. These explain a rule but should live in the planning log.
- Buried: the Two gates paragraph is now above Working style (good), but it is one dense paragraph of ten numbered clauses and it points to "the rules below are the detail", which means a reader must cross-reference. The self-audit rule (C49) and the verbatim-quote requirement have no line in the gate that names the file to produce.
- Not in the file at all: prototype UI spec, owner never opens the editor, mute video, don't rush a stop, think about parallelism, never kill other processes, free-first.
- Missing pieces:
  1. **A standard brief template** (the main session writes every brief from scratch; coverage varies, section C).
  2. **A standard report/STATUS template** (formats differ by agent; the best, #126, was written by the main session by hand).
  3. **A script that refuses a hand-in without the required files.** Nothing exists (`tools/` has only `blender_kit/` and one fetch script; no `.claude/` folder, so no hooks, skills or settings carry rules).
  4. **A way to carry the contract to worktrees** independent of the branch age.
  5. **A "where do composites go" rule** (conflict 3 in section A).
  6. **A decision-log hygiene rule**: P38 (gore paved) is superseded by later work without a note; H3 and H8 go stale within hours.
- HANDOFF.md is 176 lines and was rewritten at 15:00 local; by 15:15 it was incomplete. It restates §1 rules that are in CLAUDE.md in more words (12 of 13 §1 bullets are copies or extensions), so every new rule has to be edited in two to four files, and one is always missed.

---

# G. Root causes, ranked

Evidence is listed with each cause. "Main" = main session's behaviour; "Docs" = the documents' fault.

1. **Rules do not reach the agents that need them (Docs + Main).** Verified: the CLAUDE.md in the junction, earthworks and cover worktrees lacks the downloads approval, nothing-fails-silently, the image check and (junction) even the reference-first rule (B). Brief #123 cites lines that do not exist in that copy. The junction agent refused to download because "that approval reached me only through the coordinating agent" (brief #120 text; STATUS). The self-audit rule reached builders only as chat relays #207-#210 at 14:13Z, which is the same fragile route. Main session behaviour: it knew worktree files are separate (it wrote "your worktree already exists" and the ownership lists) but did not check the rule file in them; docs fault: nothing tells it to.
2. **The briefs set the item list and a time-box as the finish line (Main).** Rule 1 existed from 08:59Z. #123 (13:40Z): "these are the whole task". Every brief: "Time-box about N hours ... report the rest as not done". Result: honest reports of undone items (r7 wall ends, junction items 3-5) that the main session re-briefs as new items next round. This is the best single explanation for "I just don't see much progress round on round". It also matches the owner's "lazy in the fixes" guess: the agents did what the brief allowed.
3. **Rules were added after the failure as appended one-liners, and the briefing routine changed only at 14:15Z (Main + Docs).** Chronology of rules: 08:59Z reference-first (after the owner's complaint), 13:21Z confirm images (after "look yourself"), 13:25Z downloads/silent (after the refusal), 14:13Z self-audit (after "lazy in the fixes"), 14:15Z gates. Each rule closed a failure that had just occurred; nothing was written to stop the next class of failure. Memory notes record the same pattern ("Why: On 2026-10-11 ... the user said ...").
4. **No acceptance routine until the owner forced one (Main).** The roundabout claim at 13:17Z was passed on unseen even though the memory note of the preceding day on reference claims said not to relay unverified claims (`reference-claims-need-opened-photos.md`, 10 Oct). The Gate 2 now says to open images; whether it is followed is unverified. Owner's "how well are you actually keeping track of these rules?" (14:14Z) has an honest answer from this audit: **the main session keeps them in its conversation memory, not in a checklist, and it forgets them at compaction; the repo copies are too numerous and too different to rebuild them from.** I cannot show what the main session's context contained; this is inferred from the pattern of repeats and from the owner's remark "I did compact you".
5. **No mechanical checks (Docs).** All checking is the main session's attention. A side-by-side image requirement exists but contradicts the private-photo rule and names no folder, so it is not performable (A, contradiction 3).
6. **Contradictory or capped guidance on parallelism and resume (Docs).** C31 vs C39 vs H141; "single lane" and "3-4 agents" are the only quantities, no trigger for splitting. The owner asked at 22:46Z "can you be doing more than this one agent", 07:19Z "I feel like this can be done in parallel", 12:43Z "only 2 agents running?", 13:52Z "just generally please think about parallelism". Only the last has a written home (a generic line in C31). Main behaviour: it queued work behind the Unreal lane and the usage cap without telling the owner the blocking reason.
7. **Owner instructions that never get written down (Main + Docs).** "Don't rush a stop" (two prompts: 09:42Z, 09:43Z) is nowhere in the repo or memory. The main session sent the opposite message one minute after the first prompt.
8. **Second-hand paraphrase of the owner's words (Main).** 6 of 8 visible-work briefs paraphrase the owner. Paraphrase lowers weight: the junction agent treated a relayed approval as unverifiable; the repo's own CLAUDE.md note says "agents trust quotes in CLAUDE.md over relays".
9. **The finish line is set by who reviews, not by what the photo shows (Docs).** Self-audit is described only in prose; it is not tied to an artifact.

Plain division: causes 2, 4, 7, 8 are the main session's behaviour. Causes 1 (partly), 3, 5, 6, 9 are the documents' and tooling's fault. Cause 1 is both: a procedural gap the main session should have caught and a document set that gives no instruction for it. I cannot rank "the main session forgets" separately from "the documents are too many": the evidence (the same rule written in five places, three of them stale) supports that neither side is ever reliably authoritative.

---

# H. Fix list (ordered)

Each item: files, action, type. SM = safe-mechanical (an agent can do it without a decision), OD = needs the owner's decision. No owner rule is dropped; where one moves, its home is stated.

1. **Create `docs/AGENT_CONTRACT.md` (SM for structure, OD for wording of rules 6, 21, 29).** About 30 lines, imperative, no history. It is the one place the rules for agents live. Content, in this order:
   1. Photos are the target: open >=3 per case before designing; name them; say where none exists; fetch (pre-approved, quote); never copy them outside `docs/reference/`.
   2. Finish line = the photo. Self-audit loop (render at reference framing, compare, write fault list answering "what would a driver, engineer or reviewer say is wrong or would never be built", fix, repeat up to 3 passes), hand in the files in item 3.
   3. Hand-in files (list in the report template).
   4. Skipped/refused/failed/unverified go in the first lines of the report and STATUS.md.
   5. Never report a visible thing fixed that you have not opened; name the image.
   6. Worktree, own build dir, clean builds, never kill other processes, background only, `[skip ci]`, no push.
   7. Mute video; no downloads other than reference photos without the owner.
   8. At the usage limit: finish the current verified step, commit, update STATUS.md, carry on unless told. (Text for the main session's standard message is in item 10.)
   9. Project facts that every agent needs: prototype UI is the spec; owner never opens the editor (agents run and test); structure first; free-first.
   CLAUDE.md gets one line pointing at it; the main session pastes it into every brief (item 3).

2. **Rewrite `CLAUDE.md` working-style section into ~30 short lines, grouped (SM, with OD on the contradiction choices).**
   - Group 1 "Main session: before briefing" (the Gate 1 as a checklist of 8 bullets) and "Main session: before accepting a report" (Gate 2 as a checklist of 6 bullets), each bullet one line with its file or command.
   - Group 2 "Agents" -> pointer to AGENT_CONTRACT.md.
   - Group 3 "Project rules" (commands, IDs, coords, driving-side, region data).
   - Delete the duplicate line 44; delete "last CI-verified commit" and dates (move to planning log); fold C48 and C49 text into the contract.
   - Resolve (OD): resume vs fresh. Recommendation: **fresh agent per round from STATUS; resume only for follow-ups under ~3 messages**. This matches the owner's efficiency instruction (HANDOFF/memory) and what the main session actually did. Delete the contrary phrases in C31/C39.
   - Resolve (SM): parallel vs cap: replace both with "Split independent work and launch it together; the limits are 3-4 live agents, one Unreal lane (code may be compiled in another worktree while one agent runs the editor). Before launching fewer agents than there are independent items, state the blocking reason in the brief gate."
   - Resolve (OD): C29 "ask the person often" -> "Ask about product direction, scope and how tools should feel to use; decide road-engineering details and give the owner a toggle."
   - Resolve (SM): the photo minimum: "open at least 3; a score resting on fewer than 2 says so" (one sentence, both numbers explained).
   - Add (SM): "composites go in `docs/reference/_compare/<round>/` (gitignored) with a committed manifest `COMPARE.md`" (see item 6).

3. **Brief template and generator (SM).** New `docs/templates/BRIEF.md` and `tools/make_brief.mjs` (node, since node is installed and the Windows Python alias is not): the script takes a short body file, prepends `docs/AGENT_CONTRACT.md` verbatim (read from the main checkout absolute path) and the filled header, refuses to output if mandatory fields are empty. Proposed template:

   ```
   TASK: <one sentence>   ROUND: <name>   WORKTREE: <path>  BRANCH: <name>   BASE: <commit, must be current main>
   OWNER'S WORDS (verbatim, with date): "<quote>" ; "<quote>"
   FINISH LINE: the output matches the reference photos in the cases below. The list of faults further down is a starting list, not the task.
   CASES AND PHOTOS (absolute paths in the MAIN checkout; >=3 per case; say where none exists, then fetch; fetch is pre-approved: owner's words "If you ever need more reference images go get them" / "yes please allow them to download photos as and when needed", also in CLAUDE.md of the main checkout):
     - case | photos
   KNOWN FAULTS (starting list):
   SELF-AUDIT (required): render each case at the framing of its best photo; make a composite (render | photo | before) into docs/reference/_compare/<round>/; write FAULTS.md pass 1: "what would a driver, engineer or reviewer say is wrong or would never be built?" including items not in the list above; fix; re-render; pass 2; pass 3; stop when the Residual list holds only items you can name a reason for.
   HAND IN (files, in docs/progress/<round>/): STATUS.md (first heading "Skipped / not done / not verified"), REFERENCE_NOTES.md (per case: Opened: ...), FAULTS.md (Pass 1..n + Residual), COMPARE.md (table: case | render | reference | composite | before-same-camera), INDEX.md, suite counts.
   STOP RULE: you are done when Residual is justified, not when time is up. If you must stop (usage limit): commit, update STATUS.md first, continue after reset.
   PARALLELISM NOTE (main session fills): independent items launched now: ...; not launched and why: ...
   MUTE VIDEO / NO DOWNLOADS OTHER THAN REFERENCE PHOTOS / NEVER KILL OTHER PROCESSES / BACKGROUND / [skip ci]
   REPORT: first lines = skipped/refused/unverified; then ... <= N words
   ```
   Replace the "Time-box about N hours" line with the STOP RULE above (OD: the owner may want a ceiling; recommendation: a ceiling expressed as "pass 3 then stop and report the residual", not hours).

4. **Report/STATUS template (SM).** `docs/templates/STATUS.md`: sections in this order: `## Skipped / not done / not verified`, `## Reference files opened` (per case), `## Own fault list and residual`, `## Changes (file:function)`, `## Images that carry each claim` (table: claim | image file), `## Suite / checks`, `## Next step`. Fixed headings so the check script can find them.

5. **Hand-in check script `tools/check_handin.mjs` (SM).** Usage: `node tools/check_handin.mjs <progress-folder> [--main <path>]`. Exit 1 with a list if any of:
   - `STATUS.md` missing or first `##` heading is not "Skipped / not done / not verified", or has an empty body;
   - `REFERENCE_NOTES.md` has any case with fewer than 3 `Opened:` files (or no `No photo covers:` + fetch line), or a file named does not exist under `<main>/docs/reference/`;
   - `FAULTS.md` missing, fewer than 2 passes, or no `Residual` section;
   - `COMPARE.md` rows whose render/reference/composite/before files do not exist;
   - `STATUS.md` contains "fixed", "done", "good" within a sentence that names no image file (warning, not failure);
   - the progress folder contains image files from `docs/reference/` (hash compare, to protect the private-photo rule);
   - the branch's last commit date is before the newest render (stale renders), and the worktree's `CLAUDE.md` hash differs from main's (warn).
   Main session runs it before reading any report; failing output is pasted back to the agent unread (that is Gate 2(2)). Agents run it too (the contract tells them to).

6. **Fix the composites vs private-photos conflict (OD).** Recommended: composites live in `docs/reference/_compare/<round>/` (already gitignored via `docs/reference/`); the committed `COMPARE.md` holds names and the agent's verdicts only. Precedent: `docs/reference/gsv/compare/` used in `unreal-second`. The owner can open the composites from the main checkout; if the owner reads on a phone, the main session sends them as files (existing rule).

7. **Worktree creation script `tools/new_agent_worktree.ps1` (SM).** Creates the worktree from the current main HEAD (or merges main first), creates an NTFS junction `docs/reference` -> main checkout's, prints the CLAUDE.md hash, and writes `docs/progress/<round>/STATUS.md` from the template. Add to the contract: "Never start from a stale branch: run `git merge <main>` first." Also **bring the three stale worktrees up to date now** (`-wt-grade`, `-wt-junc`, `-wt-cover`): merge main into each, which also pulls CLAUDE.md and the contract (SM, but conflicts in region files are expected, HANDOFF §8 warns).

8. **Single-source the rules in HANDOFF, planning log and README (SM).** `HANDOFF.md` §1 and §10: keep the owner's quotes and the product decisions; replace restated agent rules with "see `docs/AGENT_CONTRACT.md` and CLAUDE.md". Remove HANDOFF §10 bullet duplicates. Refresh H3, H8 (HEAD, worktree map) and add a rule that HANDOFF carries a "last verified against git at <time>" line. `planning-log.md`: add Round 49 recording the contract, Two gates, self-audit, and a note on Round 38 that gore paving was superseded (SM). `research/vegetation-plan.md:43`: remove "lighting columns" from the Ignore list and add "(judged; see Round 35)" (SM). `README.md`: add the contract, templates and script to the index.

9. **Hook (OD, unverified feasibility).** If Claude Code hooks can run a command when a subagent stops, add a project hook that runs `check_handin.mjs` on the agent's progress folder and prints the result into the main session. Check the Claude Code documentation before relying on it; the repo has no `.claude/` folder now. Needs a settings change, so it is the owner's call.

10. **Standard limit message (SM, text OD).** Put in the contract: "At 90% of the window the main session sends exactly: 'The usage window is nearly full. Finish the current verified step, commit, update STATUS.md (skipped items first), then continue. Do not start something new and do not shorten your report unless the owner says to.'" Delete the 09:43Z style of message. Add "do not rush a stop" in the owner's words.

11. **Acceptance log (SM, one line of process OD).** Gate 2 produces `docs/progress/<round>/ACCEPTANCE.md` written by the main session: images opened (paths), what it saw, "would never be built" answers (>=3 items or the reason there are none), what went back to the agent, what is sent to the owner. The owner can ask to see it. Prevents "relayed without opening".

12. **Parallelism line in the gate (SM).** In Gate 1: "List independent work items; launch all that do not share files, the Unreal lane or the usage cap; for each held back, write the reason in the brief header. If fewer than 2 agents are running, say why to the owner in the same message." That is the checkable form of "think about parallelism" (owner prompts: 22:46Z, 07:19Z, 12:43Z, 13:52Z).

13. **Memory notes (SM).** Add two missing feedback notes: `dont-rush-a-stop.md` (09:42Z/09:43Z, with the owner's words) and `think-about-parallelism.md`; update `agents-self-audit-before-review.md` to point to the contract; add a line to MEMORY.md index. Memory is main-session-only, so these do not replace item 1.

14. **Brief hygiene (SM, immediate).** In every brief remove "these are the whole task" and "time-box" language; quote the owner verbatim; give the main-checkout absolute path to the references; add the mute-video line to research briefs; and change commit trailers to match the current attribution rule. Also resolve the first two bullets of §D: re-send the contract text to the running agents (`-wt-grade`, `-wt-junc`, `-wt-cover`) as one message each, quoting the owner's three sentences, since their project files do not carry it.

Order of work for a second agent: items 1, 8 (cleanup), 2 (needs owner decisions on resume and ask-often first), 3, 4, 5, 6, 7, 10, 11, 12, 13; item 9 and the wording of 1 (rules 6, 21, 29) wait for the owner's reply.

## Appendix: evidence references

- Owner messages (transcript UTC): 07:25:54 "remember there are probably ref images from aerial views in the repo"; 08:54:13 "You've got so many references how are you making these mistakes still"; 08:58:49 "All the time you should be using reference images ..."; 09:15:12 "If you ever need more reference images go get them"; 09:42:53 "don't rush a stop"; 09:43:57 "I said don't rush a stop"; 09:45:43 "Aerial ref images needed"; 12:43:57 "only 2 agents running?"; 13:17:55 "did you not attempt to fix the roundabout arms then"; 13:19:09 "look yourself at the first of those images"; 13:20:44 "when a sonnet agent says its good, you then also review"; 13:23:54 "i take it the agent is designing its markings from the reference images yes?"; 13:25:18 "yes please allow them to download photos ... im annoyed you let that fail siliently"; 13:48:42 "photos being fetched ... often very far view aerial"; 13:52:06 "just generally please think about parallelism"; 14:12:42 "i just don't see much progress round on round ..."; 14:13:59 "i swear ive told you this so many times"; 14:14:51 "how well are you actually keeping track of these rules?".
- Briefs: #90 (08:55Z), #93, #94-#95 (09:15Z), #98, #99-#106 (09:43-09:44Z), #107, #113, #114, #118, #119 (13:19Z), #120-#122 (13:25Z), #123 (13:40Z), #124, #125, #126-#129 (13:52-13:54Z), #207-#210 (14:13Z).
- Extracted briefs and owner messages are in the scratch folder `...\scratchpad\audit\briefs.txt` and `um.txt` (outside the repo).
- Worktree CLAUDE.md check: `roadworld-wt-grade`, `-junc`, `-cover` (43, 42, 43 lines), `-m2`, `-tool2`, `-veg` (45 lines) against main (50 lines).

---

# I. Mechanical enforcement (added 11 Oct 2026, after the hooks were built and live-tested)

What exists: `.claude/settings.json` wires four hook entries to `tools/hooks/`: `brief_gate.mjs` (refuses a subagent launch whose prompt lacks the contract), `contract_inject.mjs` (SubagentStart: puts `docs/AGENT_CONTRACT.md` of the main checkout into every non-exempt agent), `handin_gate.mjs` on SubagentStop and on the SubagentHandback tool (an agent cannot hand in or stop until `check_handin.mjs` passes; 3 refusals, then it is let go and `HANDIN_FAILED.txt` is written). Every decision is a line in `.claude/state/gate.log`. All three FAIL OPEN on any internal error (a log line "FAIL-OPEN" then means a gate did not run), because the main session delegates all code changes: a gate that wrongly blocked every launch would deadlock it.

Verified live with the bundled Claude Code CLI 2.1.295 (headless, throwaway repo, details in `docs/progress/2026-10-11-hooks/STATUS.md`): launch refusal reaches the model; SubagentStart context reaches the agent; the hand-in gate fires for background agents, its message reaches the agent, and the counter lets it go after 3.

| # | Root cause or repeated complaint | Verdict | Closed by / what remains |
|---|---|---|---|
| 1 | Stale rules in worktrees (G1) | **Partly** | `contract_inject` hands every new agent the main checkout's current contract whatever its worktree's age; `brief_gate` also forces the contract (and the owner's quoted words) into the prompt. REMAINS: the agent's own `CLAUDE.md` copy is still old (only `new_agent_worktree.ps1`, run from current main, fixes that); agents launched before the hooks were merged are not re-briefed (see 7). |
| 2 | Item list and time-box as the finish line (G2) | **Partly** | `brief_gate` refuses a prompt containing "time-box" or "whole task"; `make_brief.mjs` refuses it in the task file. REMAINS: a string match. "About 3 hours", "these items are the job" or "stop after item 4" pass. Whether a brief's finish line really is the photo is judgment; a reviewer or the owner reads it. |
| 3 | References not opened (G1/B, C) | **Partly** | `check_handin.mjs` (run by the gate) requires `References opened: N of M`, a "not opened" line per missing file, >= 3 named files per case that exist on disk. Gap that existed and is now fixed: references that are not photos (prototype screenshots, spec documents, absolute paths) could never pass; they now resolve against absolute paths, the main checkout and the hand-in's worktree. REMAINS: it proves the names exist, **not that the agent opened them**. An agent can list files it never looked at. Only a cold-read reviewer who checks the agent's claims against the images catches that. |
| 4 | Claims relayed unseen; main session did not open the image (G4, D) | **Cannot be enforced** | The hooks see agents, not the main session's reasoning. `ACCEPTANCE.md` stays a log the main session writes by hand. A main-session `Stop` hook that refuses to end a turn while an agent report lacks an ACCEPTANCE entry could be built, but it would only check that a line exists, not that an image was looked at. Human step: the main session opens the headline images; the owner can ask for ACCEPTANCE.md. |
| 5 | Skipped items buried at the end (D) | **Partly** | The gate refuses a STATUS.md whose first heading is not "Skipped, refused or failed" or whose body is empty. REMAINS: an agent can write "Nothing skipped" while having skipped things. HANDIN_FAILED.txt makes a forced stop visible, but only if the main session reads it. |
| 6 | Main session not running the gate (G4) | **Partly** | The hand-in half now runs without the main session: no agent can hand in without the check passing (or being logged as failed). REMAINS: the other half of Gate 2 (open images, same-camera movement, QUEUE, commit, pictures to the owner) and Gate 1 items about the content of the brief (photos chosen well, parallelism reasoned) are still the main session's. |
| 7 | Old briefs still running (B, step 14) | **Not solved** | Agents already running when the hooks are merged were briefed without HANDIN_DIR, so the hand-in gate lets them stop (no HANDIN_DIR = allow). Whether SubagentStart/Stop apply to agents started before a settings change was not tested. Human step: re-send the contract by message, or let them finish and run `check_handin.mjs` by hand. |
| 8 | Parallelism ("think about parallelism") (G6) | **Cannot be enforced** | `make_brief.mjs` refuses empty Launched/Waiting lines, but a launch with `NO-CONTRACT:` skips it, and no hook can judge whether a held-back item had a real blocking reason or count how many agents run. Human step: the main session writes the reason and tells the owner when fewer than 2 agents run. |
| 9 | Queue kept in the main session's head (G4) | **Cannot be enforced** | Nothing observes the main session's state. `docs/QUEUE.md` stays a convention. Possible cheap aid, not built: the gate log already lists every launch and every hand-in; the main session can rebuild "who is running" from it after a compaction. |
| 10 | Owner's words paraphrased (G8) | **Partly** | The gate demands a non-empty "Owner's words" section containing quotation marks. REMAINS: it cannot check that the quote is verbatim or real. |
| 11 | Download approval and other rules reaching agents second-hand (G1) | **Closed for delivery** | The contract (with the owner's two quotes verbatim) is injected first-hand into every non-exempt agent. REMAINS: an agent can still ignore it. |
| 12 | "Don't rush a stop", mute video, never kill processes, never ask the owner to open Unreal | **Partly** | Delivered to agents by the injected contract. Not enforced: no hook can see a video or a killed process. The main session's own "stop now" messages are not checked by anything. |
| 13 | `NO-CONTRACT` as an escape hatch | **New gap created by the design** | Any launch can bypass the contract with a 20-character excuse; a build agent launched that way still gets the injected contract but no hand-in gate (no HANDIN_DIR). Mitigation: every use is logged with its reason; the main session or owner can grep `no_contract_reason` in `gate.log`. |
| 14 | Hooks not running at all | **Residual risk** | A mistyped path or `node` not on PATH makes a hook a silent non-blocking error (default `onFailure: continue`). `onFailure: "block"` would turn that into a deadlock risk, so it was not set. Check `gate.log` has a fresh line after the first real launch. |
