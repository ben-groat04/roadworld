# Gates round 10: phase agents, gated audits and first looks, and the 11 faults owed from the gates9 audit

## Owner's words
23 Oct 2026, on long builders: "is it not worth getting the long running agents to delegate to other subagents, ideally sonnet, as they are also racking up context and tokens, so theyre' not really offering much"
23 Oct 2026: "and ofc it needs to work with the gate system, have you been sidestepping it?"
22 Oct 2026: "when you audit, be strict and ensure EVERYTHING gets addressed in a fix. no excuses for not fixing issues unless its like so pedantic i wouldn't notice or you aren't sure the audit is picking things up right"

## Scope
This is one tooling round with three parts: the phase mode, the gated audit and first-look launches, and the gates9 residual faults. They share the same three files (make_brief.mjs, check_handin.mjs and the finish/launch hooks), so they cannot be split without merge conflicts.

## Cases
Engine and tooling work, no renders: HANDIN: no-visual.

Files to open, all of them (M = 10):
- C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates9\AUDIT.md (the 11 faults owed)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\progress\gates9\STATUS.md
- C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\make_brief.mjs
- C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\check_handin.mjs
- C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\tools\hooks (every file in the folder)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\.claude\settings.json
- C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\AGENT_CONTRACT.md
- C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\docs\templates (every file)
- C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10\CLAUDE.md (sections "Hooks enforce", "Two gates", "Main session")
- C:\Users\bgbg0\Documents\Programming\roadworld\.claude\state\gate.log (read only: the 23 Oct NO-CONTRACT auditor launches are the evidence for part B)

### A. Phase agents
A builder round may run as up to three short fresh agents in sequence, each handing over through a file in the round's hand-in folder:
1. `measure`: opens every reference and writes MEASURE.md.
2. `build`: reads MEASURE.md and the decisions, writes the code and tests, and writes BUILD.md.
3. `render`: makes the renders, composites and self-audit, and writes the full hand-in.

**make_brief.mjs.** Add `--phase measure|build|render`, used together with `--round`.
- It prints the contract plus a phase section saying what the phase does, what it reads, and what it must write.
- The launch prompt carries a `PHASE: <name>` line.
- The `build` and `render` phases refuse to brief unless the earlier phase's file exists and passed its check.

**check_handin.mjs.** Add `--phase measure|build`.
- *measure* requires MEASURE.md with:
  - a `References opened: N of M listed` line;
  - one measurement table per case, each row naming the photo file and the measured value in metres or lane widths;
  - at least one trace or overlay image per case, newer than the brief and decodable.
- *build* requires BUILD.md with:
  - the commit list since the measure phase;
  - the gcc suite result run by the check itself, not quoted;
  - a fail_first_*.txt file for each new test;
  - no new waivers, using the existing guard;
  - an empty `git status`.
- *render* is the existing full hand-in check, unchanged.

**Finish hook.** A phase agent may stop when its own phase check passes. Each phase agent gets its own 3-refusal count. This does not use the one-per-round CHECKPOINT allowance.

**Audits.** `--accept` and the audit still apply to the round as a whole, after the render phase.

### B. Gated audits and first looks
- **Opus first look.** Add `make_brief.mjs --first-look <progress-folder>`, which prints an Opus first-look brief.
  - The agent must write FIRSTLOOK_OPUS.md, with an `Opened:` line naming the images and photos, a top-3 list, and a "what here would never be built" line.
  - It must not read AUDIT.md, STATUS.md or FAULTS.md first; the brief says so.
  - The finish hook refuses the stop until the file is valid.
- **Launch hook.** Refuse a NO-CONTRACT launch whose description or reason names an audit, an auditor or a first look. The refusal text says to use `--audit` or `--first-look`. Clerks, merges, research and read-only checks stay allowed.
- **Tests.** Add tests for both, using the real 23 Oct launch descriptions from gate.log as REFUSE cases ("Auditor for gates9 hand-in", "Opus first look on grade-r12 images"). Use clerk, merge and research descriptions from the same log as ALLOW cases.

### C. The 11 gates9 audit faults
Fix every item in gates9 AUDIT.md (contract item 17):
- the H3 depth bound;
- the H7 forms: bulk formatters, astyle, `ed`, `sort -o`;
- the truncated-PNG check;
- the A6 limit measurement;
- the test temp prefix;
- test_backup_reference with CP_GATE_STATE_DIR set;
- the patch, `git apply` and fs.rm write routes;
- the LOCK-LIST-BAD warning channel;
- the delete_guard false positive on the text "Remove-Item -Recurse" inside a heredoc or a commit message.

### Proof
- All eight suites stay green.
- Every new REFUSE and ALLOW case is shown failing on main code (fail_first_*.txt).
- No test writes the live gate.log; use CP_GATE_STATE_DIR.
- A STATUS table runs each new gate against a made-up good and a made-up bad phase hand-in.

### Merge text
Put the contract and CLAUDE.md text for the phase rule in STATUS for the merger. Do not edit main.
- The contract text should say that rounds may be split into measure, build and render phases, and that the main session splits every look round.

### Worktree and ownership
Worktree `C:\Users\bgbg0\Documents\Programming\roadworld-wt\gates10`, branch `agent/gates10`, cut from main 5fe652c.
- You own `tools/`, `docs/templates/`, and `.claude/settings.json` in your worktree only.
- No Unreal. Do not push or merge.

## Faults
Known: the 11 items in gates9 AUDIT.md, and the missing gate coverage for auditor and first-look launches (part B).

## Launched
This job. Running: builders for roundabout5, tjunction6, m2fix5b and m3ui-r1.

## Waiting
grade-r13 waits for this round's phase mode, so that it runs split and gated.
