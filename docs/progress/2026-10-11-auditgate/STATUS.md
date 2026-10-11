## Skipped, refused or failed

- Nothing refused or failed. NOT TESTED LIVE: the whole audit gate was exercised only with synthetic hook stdin in temp copies (`node tools/hooks/test_hooks.mjs`, 237 pass, 0 fail). Not tried in a real Claude Code session: (a) that `agent_id` in SubagentStart and SubagentStop is the same string for one agent (the design depends on it; the existing gate already keys its counter on it), (b) that the YOUR AGENT ID line in the injected SubagentStart context reaches the auditor, (c) a real auditor agent writing AUDIT.md and being released, (d) the `--since` freshness check against real transcripts (the first-line `timestamp` field is assumed; falls back to the file creation time). Needs a live run by the independent tester.
- Not fixed from the audit of the gate notices (`docs/progress/2026-10-11-gate-notices/AUDIT.md`), reasons: fault 3 (launch gate covers only the Agent tool; other spawn paths and exempt types) needs a live look at which tools fire SubagentStart, not done; fault 6 (junk NO-CONTRACT reasons, fake contract) cannot be judged by regex, the notice already lists them; fault 7 and 8 (empty stdin prints no pending notice; BOM or truncated log line) the audit rates low, untouched; fault 9 (ack race) disappears with per-item acks. Fault 2 is only partly fixed: acks are per item and need a code shown only in the main session's notice, but a subagent that deliberately reads `.claude/state/ack_secret` can still ack, and deleting gate.log still silences the notice.
- Fixed with tests: audit faults 1, 2 (per-item), 4, 5, 10, plus the COMPARE relevance fields and the whole builder/auditor gate.

References opened: 10 of 10 listed (also the gate-notices AUDIT.md sent mid-task, 11th)

## Reference files opened

Not visual work, no photos. The spec files and tools named in the brief, all read in full or in the relevant sections.
Case: Tooling | Opened: tools/check_handin.mjs, tools/make_brief.mjs, tools/hooks/lib.mjs, tools/hooks/brief_gate.mjs, tools/hooks/handin_gate.mjs, tools/hooks/gate_notice.mjs, tools/hooks/notice_lib.mjs, tools/hooks/test_hooks.mjs, .claude/settings.json, docs/research/rules-audit-2026-10-11.md

## Own fault list and residual

See FAULTS.md (Pass 1, Pass 2, Residual).

## Changes (file:function)

- `tools/hooks/audit_lib.mjs` (new): `auditOfIn`, `recordBuilder`/`readBuilder`/`allBuilders` (state `.claude/state/builders/<key>.json`), `parseAudit`/`checkAuditFile` (first-lines format, different id, id equals the finishing agent, not older than the run), `auditOwed`, `pastedFaultList`.
- `tools/make_brief.mjs`: `--audit <progress-folder>` prints the auditor brief from `docs/templates/AUDIT_BRIEF.md`; owner-words exempt from the time-box test.
- `tools/hooks/brief_gate.mjs`: AUDIT-OF launches allowed, refused on relative folder, HANDIN_DIR present, or pasted fault list.
- `tools/hooks/handin_gate.mjs`: records builder id and logs `audit owed` on a passing hand-in; AUDIT-OF agents held until a valid AUDIT.md; `blockOrRelease` shared; `--since` freshness; counter reset on pass.
- `tools/hooks/contract_inject.mjs`: YOUR AGENT ID line.
- `tools/hooks/notice_lib.mjs`, `gate_notice.mjs`, `gate_ack.mjs`: AUDIT OWED section (computed from files, no ack clears it); per-item acks with codes; canary RULE 3.
- `tools/check_handin.mjs`: `--accept [--builder id]`, `--since`, COMPARE rows need subject/type, scale, viewpoint match cells.
- Docs: `CLAUDE.md` (second gate, ack usage), `docs/AGENT_CONTRACT.md` item 15, `docs/templates/AUDIT.md`, `AUDIT_BRIEF.md`, `BRIEF.md`, `docs/research/rules-audit-2026-10-11.md` rows 15 and 16.

## Images that carry each claim

No images (tooling). Evidence is the test run below.

| Claim | Image file (opened by me) |
|---|---|
| none: not visual work | n/a |

## Suite and checks

`node tools/hooks/test_hooks.mjs`: 237 passed, 0 failed (was 110 before this branch). `node --check` on every tools/*.mjs and tools/hooks/*.mjs. Sample `make_brief.mjs --audit` output read by me (scratchpad `audit_brief_sample.md`). Main-checkout CLAUDE.md is newer than this branch (it already has the auditor paragraph in Main session and item 3 of the second gate); expect a small textual merge in CLAUDE.md.

## Next step

Independent agent tests it live (items a-d above). Then merge into main and restart nothing: hooks are read from the main checkout.
