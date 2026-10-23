# FAULTS, round gates4 (self-audit of the gates built this round; attacks run against my own code)

## Pass 1

Attacks on the first complete version (picture, BLOCKED, waiver, hooks), faults found and what was done:

1. The mask-based picture gate accepted a backticked judgement as a concrete anchor in BLOCKED ("blocked by: `the complexity of it`"): backticks alone made it look concrete. Fixed: quoted text only counts when it can be looked up (a path, command, identifier or number) and is not a vague word. Tests "a backticked judgement is not a concrete anchor" and "...but a backticked command or identifier is".
2. A before picture that differs from the render in ONE pixel passed the identity check (the hash differs). Fixed: render and before of the same size must differ clearly in at least 0.05 % of the pixels (`MIN_MOVED`). Tests with a one-pixel edit (refused) and a 13 % change (passes).
3. An Items row "done | it works fine now" passed (15 characters). Fixed: the proof must name a file, a quoted test, a command or a number. Test added.
4. The delete guard missed recursive deletes written as one-liners in other languages (python `shutil.rmtree`, node `fs.rmSync(.., { recursive: true })`, `Directory.Delete(path, $true)`). Fixed with a rule near a worktree or docs/reference; 4 tests.
5. The first real run on the tjunction2 hand-in flagged `tests/CMakeLists.txt` as a "test data / golden file" (a new test source in CMake is normal). Fixed: CMake files are excluded from the data rule.
6. `remove_agent_worktree.ps1` returned nested arrays that PowerShell flattened: the link loop saw single characters ("Unlinking d link"). Found by the scratch-repo test before any real use; rewritten with objects in a list; 18 tests, photos untouched in the real checkout (3 files before and after in the scratch test, real docs\reference listed again at the end).

## Pass 2

Attacks on the status reader and waiver guard with fresh wordings:

7. 24 new phrasings of "not done" ("No attempt was made at the island.", "Nothing was done about the markings.", "Ignored the slip lane.", "Leaving the signals as is.", "Hatching: pending.", "Only the kerb was done." ...): 23 of 24 passed unseen. Fixed: 14 patterns added, 19 of the phrasings are now refused and tested; honest sentences stay clean (4 control sentences).
8. A phrase list is never complete, so omission needs a structural check: `make_brief` now emits ITEM lines (from "## Items", else the numbered lines of "## Faults"), the finish hook passes them to check_handin, and an item with no STATUS row is refused by name (tests: an item left out, "partly", a vague BLOCKED row, no Items section, fallback from Faults).
9. `CHECK(a.width > 3.0)` -> `CHECK(b.width > 3.0)` (a different value) was caught by nothing once the left sides differed. Fixed: `assertion_retargeted` (pairs old and new comparisons by their left side). One existing gates2 test that said such a line was NOT a finding was rewritten (the gates3 audit lists this very evasion).
10. The reference count check accepted "10 of 12" next to 11 named files. Fixed: opened + not-opened lines must equal the listed total and may not exceed the files named.
11. Real runs: the roadtool3 STATUS says "The Unreal M2 branch was not touched": refused as an undone-item wording (kept on purpose: it is the wording the gates2 audit listed; scope statements say "not affected"). The roadtool3 "References opened: 48 of 48" against 45 named files is refused (the count cannot be verified from the file).

## Pass 3

Attacks on records, hooks and gates as a whole:

12. A made-up auditor id with a correctly signed record ("fake9"): the audit stays owed (needs a SubagentStart line for that id, which only the launch hook writes). Test with a second, genuine id that clears.
13. The fake hand-in of the gates3 audit (one gradient PNG as render, reference, composite, before, three reference copies, typed record with the right hash and road_px 60000): refused end to end with five distinct reasons (same bytes in roles, one picture as three references, no mask picture, copies not registered). Test "the gates3 audit's fake hand-in".
14. A mask that is a colour gradient, a mask the size of another picture, a mask whose road pixels are grass in the render, an orange render beside a clean mask, a mask equal to the render, typed numbers that disagree with the mask: each refused (10 tests). Real tool output: `scene.mjs` writes the mask; `cross_slope` (23.9 % road) passes, `overview` (0.31 %) is refused (same figures as before, now counted in the mask).
15. The state guard: writes, reads of the secret, greps, redirects, `gate_ack`, `ref_seed`, and commands naming `recordAudit` are refused for subagents; the main session is free; plain `cat gate.log` is allowed. The Unreal and engine builders' ordinary commands (git, check_handin, builds, `rm -rf build`) are untouched (tests).
16. The three suites were run 3 + 3 + 1 at once: all green (462, 399, 18 passes each).
17. Matcher syntax and the subagent fields were checked against the hooks documentation (code.claude.com/docs/en/hooks): `Read|Write|...` is a pipe list, `agent_id` is present on tool events inside a subagent. PostToolUse's exact field list was not readable in the part of the page fetched: the hook reads only `agent_id`, `tool_name` and fails open.

## Residual

1. Typed-record forgery with a hand-drawn render AND a matching hand-drawn mask (both consistent with each other and the tool palette) is not detected: the gate reads pictures, not how they were made. Reason (b) for leaving it: it needs a pixel-level fabrication that no check short of re-rendering the scene can refute; the auditor sees the picture.
2. A picture made up inside the round that is not a copy of the round's own pictures can be registered as a reference (the hook sees files appear, not their source; INDEX.md is agent-editable by design: agents log downloads there). `by` and `ts` in ref_log.jsonl name the agent and time for the auditor.
3. The state guard reads tool input text and paths: a script file that touches the state indirectly, or a path assembled from pieces, is not seen (same limit for the delete guard). A writer that gets round the guard could sign (the HMAC is tamper evidence only, stated in `lib.mjs`).
4. Four honest-looking rewordings still pass the status reader ("Not covered: ...", "... unchanged from before.", "Hatching not present.", "Everything except the signals."): they are indistinguishable from true statements by wording. Reason (b): flagging them would refuse honest text; the Items table catches the omission itself.
5. `ctest assertions >= N` needs the main session to enter N (the base's count) in the task file; nothing derives it automatically.
6. Thresholds 1 % road / 0.1 % debug, 0.05 % movement have no photo behind them (the figures they sit between are in `gates_lib.mjs` and STATUS); the task file overrides the first two per round.
