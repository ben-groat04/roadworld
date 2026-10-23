## Skipped, refused or failed

Nothing skipped, refused or failed in the final state. Earlier in the run the auto-mode classifier refused one Write (lock_extra.mjs) and an earlier batch of edits; the owner then added a permission rule and everything was built after that. No BLOCKED lines remain.

Not verified (said plainly):
- The three phase agents and the first-look agent were never run as live subagents through a real Claude Code launch; the hooks were driven with real-shaped hook JSON, real JSONL transcripts, real git repos and real PNGs in a temp folder (the same method as gates9). First use is the real test.
- The 23 Oct launch descriptions come from `.claude/state/gate.log` of the main checkout (read only) and from the task file; I did not re-read the whole log.
- check_handin `--phase build` runs the suite with `shell: true`; on this Windows machine that was exercised with `node -e` commands only, not with a full ctest build.
- Contract items 4, 5 and 6 of the gates9 audit were already repaired in main by d763801 (see the table); no new text is owed for them.
- "Paste-ready" merge text is in this file, below. Nothing in main was edited.

References opened: 10 of 10 listed

Case: gates9 audit and round | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates9\AUDIT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates9\STATUS.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\tasks\gates10.md
Case: tools and hooks | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\make_brief.mjs, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\check_handin.mjs, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\hooks\lock_extra.mjs
Case: contract and rules | Opened: C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\AGENT_CONTRACT.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\CLAUDE.md, C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\hooks\lock_guard_impl.mjs

The other opened files (every file of tools/hooks, docs/templates, .claude/settings.json, the live gate.log descriptions) are listed in INDEX.md.

## Prior audit items (gates9 AUDIT.md, 11 faults)

| # | Item | Status | Where / proof |
|---|---|---|---|
| 1 | H3 deep junction under build lets `rm -rf build` through | Fixed | `delete_guard_impl.mjs`: no depth bound; the walk follows only links, bounded by time (`CP_LINKS_BUILD_MS`, default 1500) and a time hit logs FAIL-OPEN (a GATE NOTICE), not a quiet allow. test_guards9 H3 depth-7 now expects refusal (54/0). |
| 2 | Bulk formatter forms (find -exec, xargs, git ls-files, `$(ls)`, astyle, uncrustify, clang-tidy, sort -o, ex, vim -es, ed, `sed -i file -e`) | Fixed | `lock_extra.mjs` + `lock_guard_impl.mjs` FORMATTERS; test_guards10 C table (REFUSE and ALLOW). |
| 3 | robocopy filter and /MOV, patch, git apply, fs.rm, ruby File.delete, brace lists | Fixed | `lock_extra.mjs` (diff names read from the diff file or a heredoc, strip levels 0-5; `--check`, `--stat`, `--numstat`, `--summary` stay allowed); `fs.rm`, `fs.promises.rm`, `fsp.rm`, `fs.open`/`openSync` with a write flag. Header now says what is still not seen (a diff piped from another command, variables, other `$(...)`). |
| 4 | Contract item 8 text wrong about scope | Already fixed in main (d763801) | Item 8 on main: G1 for every subagent, other git writes only for a HANDIN_DIR builder, NO-CONTRACT clerks and mergers may commit with explicit paths, worktree write forms listed exactly (add, remove, prune, move, lock, unlock, repair). No code identifier in the text. Checked in this worktree (cut from 5fe652c). |
| 5 | C1 backslash repair not delivered | Already fixed in main (d763801) | `docs\reference`, `C:\Users\bgbg0\roadworld-reference-backup` and the robocopy command are intact in item 8 and CLAUDE.md line 65 on main. |
| 6 | Rule text without item numbers | Already fixed in main (d763801) | Items 23 (locked tests), 24 (checkpoint), 25 (speed rules) are numbered. The new text below is numbered 26 and amends 24. |
| 7 | A6 limit raised 2000 to 3000 | Fixed | `test_guards8.mjs` A6: limit back at 2000, measured time (4 ms on this run) printed in the test message. 56/0. |
| 8 | H4 header-only PNG check | Fixed | `tools/image_lib.mjs` (`imageEndOk`: PNG IEND, JPEG FFD9, GIF trailer, BMP/WEBP size fits the file); used by the checkpoint check, the phase checks and the unreal frame check. The reference-photo-copied-into-the-hand-in floor is stated in the item 24 amendment below. test_guards10 C truncated-PNG case. |
| 9 | LOCK-LIST-BAD channel unverified | Fixed | Checked against code.claude.com/docs/en/hooks: on exit 0 `systemMessage` shows only to the user, `hookSpecificOutput.additionalContext` reaches Claude. The guard now emits both on an allowed command, and appends the warning to the stderr of a refusal (which the agent does see). test_guards10 asserts both channels. |
| 10 | clean_test_temp prefixes stop at guards8- | Fixed | PREFIXES now include guards9-, guards10-, scenegate-; `isolateState` registers exit, SIGINT and SIGTERM cleanup (links unlinked first); test_guards10 checks every `mkdtemp` prefix used by a suite and kills a child suite mid-run. |
| 11 | test_backup_reference 55/1 with CP_GATE_STATE_DIR set | Fixed | The accept_loss file goes to the state dir the tool actually uses; 56/0 with and without the variable. |
| F | delete_guard false positive (commit message / heredoc note saying "Remove-Item -Recurse") | Fixed | `codeText()` drops prose segments and non-interpreter heredoc bodies; real one-liners (powershell -Command, node -e, python -c, bash heredoc, pwsh -c) are still refused; all asserted in test_guards10. |

## What was built

A. Phase agents
- `make_brief.mjs --phase measure|build|render --round <name>`: prints `PHASE:` (and `SUITE:` lines from the task file's `## Suite`) plus a phase section; build and render refuse to brief unless the earlier phase file exists AND its check passes (the check is run by make_brief).
- `check_handin.mjs --phase measure` (MEASURE.md: References opened line, one "## Case" section each with a table naming photo file and a value in m, cm or lane widths, at least 3 different photos per case, at least one own decodable trace image newer than the brief) and `--phase build` (BUILD.md: Commits line covering every commit since MEASURE.md, hand-in-folder-only commits excepted because a file cannot list its own commit; the suite run by the check itself; a fail_first_*.txt naming each new test file; the existing waiver guard; empty git status). Code in `tools/phase_lib.mjs`.
- Finish hook: a `PHASE: measure|build` agent stops when its own phase check passes; own 3-refusal count per agent id; `HANDIN_FAILED.txt` after three; the CHECKPOINT allowance is skipped; no builder record is made, so no audit is owed for a phase. `PHASE: render` gets the ordinary hand-in check unchanged.

B. Gated audits and first looks
- `make_brief.mjs --first-look <progress-folder>`: Opus first-look brief (template `docs/templates/FIRSTLOOK_BRIEF.md`, output file template `docs/templates/FIRSTLOOK.md`), carries `FIRSTLOOK-OF:`, the owner's words, the cases and the reference list, NOT the builder's fault list or claims; says not to open AUDIT.md, STATUS.md or FAULTS.md first.
- Finish hook for a first-look agent (`FIRSTLOOK_OPUS.md`): own agent id on the file, an `Opened:` line naming at least 3 existing decodable images (at least one picture of the round and one reference photo), a Top 3 of real observations, a "What here would never be built" line, mtime newer than the run, and a transcript read-order check (AUDIT, STATUS or FAULTS opened before the first image is refused unless the file carries an `Order:` line explaining it). Own 3-refusal count and `FIRSTLOOK_FAILED.txt`.
- Launch hook: a NO-CONTRACT launch whose description or reason describes an audit or a first look is refused with the `--audit` / `--first-look` instructions; `FIRSTLOOK-OF` launches are allowed (absolute folder, no HANDIN_DIR). Clerks, merges, research and read-only checks stay allowed.
- `unreal` frame kind (coordinator's extra item): a COMPARE.md row with a 9th column "frame kind" of `unreal` (or the tag `[unreal]` in the case cell) is exempt from stats.json and mask.png and from the headline-view records, but its render must be a complete decodable image of at least 10240 bytes written during the run (`unrealFrameProblems` in `gates_lib.mjs`). The same frame without the declaration still needs its records.

## Gate runs on made-up hand-ins (every one is a case of tools/hooks/test_guards10.mjs, run through the real scripts)

| Gate | Made-up GOOD | Result | Made-up BAD | Result |
|---|---|---|---|---|
| check_handin --phase measure | MEASURE.md with opened line, 3 photos, value table, trace PNG | PASSED | 12 variants: no file, no opened line, opened 0, 2 of 3 unexplained, no case, no value, no photo, 2 photos, no image, image missing, truncated PNG, not a PNG; plus a file older than the run | each FAILED with its own reason |
| check_handin --phase build | real git repo, all commits listed, suite exit 0, fail_first for the new test, clean status | PASSED | no BUILD.md, an unlisted commit, no Commits line, failing suite, no suite named, new test with no fail_first, dirty status | each FAILED with its own reason |
| finish hook, measure phase | good MEASURE.md | allowed, logged as a phase check, no builder record | no MEASURE.md | refused 3 times (exit 2), the 4th released with HANDIN_FAILED.txt; a different agent id starts at 1 of 3; a valid CHECKPOINT.md does not let it through |
| finish hook, build phase | (via the check above) | passes | brief SUITE: command exits 3 | refused, reason names the suite |
| finish hook, render phase | | | no STATUS.md | ordinary hand-in refusal |
| finish hook, first look | FIRSTLOOK_OPUS.md with own id, 3 images, Top 3, never-built line | allowed, logged | 12 variants (no file, other agent id, no id line, no Opened, 2 images, missing image, no photo, no round picture, no Top 3, one-word item, no never-built line, placeholder) | each refused; STATUS read before the first image refused, allowed with an Order: line, images first fine; 3 refusals then FIRSTLOOK_FAILED.txt |
| launch hook | AUDIT-OF, FIRSTLOOK-OF, six real clerk, merge, research and photo-job descriptions | allowed | six real 23 Oct NO-CONTRACT auditor and first-look launches ("Auditor for gates9 hand-in", "Opus first look on grade-r12 images" and others) | refused with the right instructions |
| unreal frame | 90x90 PNG of 10 KB or more, newer than the run | passes | under 10 KB, truncated, older than the run | each refused |

## Tests

Each suite run alone, under `timeout 600`, with the isolated state dir: test_gates 514/0, test_hooks 412/0, test_guards6 32/0, test_guards7 28/0, test_guards8 56/0, test_guards9 54/0, test_worktree_tools 27/0, test_backup_reference 56/0, and the new test_guards10 107/0. Nothing reached the live gate.log (each suite checks).

Fail-first: `fail_first_test_guards10.txt` is `test_guards10.mjs` run against a scratch `git archive` of main 5fe652c's tools: 79 cases FAIL, 28 pass. The 28 are controls main already satisfies (ALLOW cases, still-REFUSE cases, isolation and the FIRSTLOOK relative-folder refusal); a few "ok" lines on old code (for example "no builder record exists") pass because the old code did nothing, so they prove nothing about the new code, and I do not count them as proof.

Where expected values came from (no test pins a fault): the gates9 AUDIT.md items (C), the owner's words in the brief (A, B), real descriptions from gate.log (B launch cases), the Claude Code hooks documentation (channels), the written rules in the brief (phase file contents).

Changed in tools/hooks/test_hooks.mjs: the copy lists of its fixture checkouts gained the new files (image_lib, phase_lib, lock_extra, phase_marks); no expectation changed.

## Merge notes

Branch agent/gates10, cut from main 5fe652c; it touches tools/, docs/templates/ (two new files) and docs/progress/gates10. Merge after gates9 is already in main. After the merge: run all nine suites (each under `timeout 600`, one at a time) and make sure `.claude/settings.json` needs no change (no new hook script, the same entry points: brief_gate, handin_gate, lock_guard, delete_guard).

## Paste-ready text for main (not applied)

### New contract item 26 (after item 25)

26. **Phases and time limits.** A builder round may be split into up to three short fresh agents run in sequence, each handing over through a file in the hand-in folder: `measure` (opens every reference, writes MEASURE.md and its trace images, no code), `build` (code, tests, BUILD.md with the commit list, `fail_first_*.txt` for each new test; the check runs the suite itself) and `render` (renders, composites, self-audit and the ordinary full hand-in). Brief each with `node tools/make_brief.mjs <task-file> --round <name> --phase measure|build|render`; build and render are refused until the earlier file exists and passes `node tools/check_handin.mjs <folder> --phase measure|build`. A phase agent cannot finish until its own phase check passes (its own 3-refusal count; the CHECKPOINT allowance of item 24 does not apply to it) and no audit is owed after a measure or build phase; the audit comes after the render phase. The main session splits every round whose look is the deliverable (junctions, earthworks, Unreal look) into these phases. Every test command is wrapped in a time limit (`timeout 600 <cmd>`), never run twice at once; a run that hits the limit means finding the slow case and fixing it, not waiting. Audits and Opus first looks are launched only with `make_brief.mjs --audit <folder>` and `--first-look <folder>`; a NO-CONTRACT launch describing an audit or a first look is refused, and a first look is not finished until `FIRSTLOOK_OPUS.md` (own agent id, an `Opened:` line naming at least three images including a round picture and a reference photo, a Top 3, "What here would never be built") passes the finish hook. A COMPARE.md row may declare its frame kind `unreal` (9th column, or `[unreal]` in the case cell): it is exempt from stats.json and mask.png but must be a complete decodable image of 10 KB or more written during the run.

### Amended item 24 (append this sentence)

The checkpoint allowance is for the final hand-in only: a `PHASE: measure` or `PHASE: build` agent gets no CHECKPOINT stop, only its own phase check. The image check is a floor (inside the folder, newer, 10 KB or more, header and end marker): a reference photo copied into the hand-in folder passes it, and the main session's own look must cover that.

### CLAUDE.md line (under "Two gates", after the Before-a-brief gate list item 6)

7. Look rounds are split into measure, build and render phases (`make_brief.mjs --phase`, contract item 26); audits and first looks are launched only through `--audit` / `--first-look` (the launch hook refuses a NO-CONTRACT audit); every test command runs under `timeout 600`, one copy at a time.
