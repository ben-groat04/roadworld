## Skipped, refused or failed

References opened: 9 of 9 listed (with one caveat below on the hooks reference page)

- Caveat on reference 9 (https://code.claude.com/docs/en/hooks): the page is 266,639 characters; the fetch tool returned digests of characters 0-200,000 only. The last 66,639 characters were NOT read. The sections I relied on (UserPromptSubmit input/output, SessionStart, Stop, PostToolUse, exit codes, the "add context" table, failure semantics) were covered.
- NOT tested live: the notice hook in the interactive desktop session (only headless `claude -p`, CLI 2.1.295). Whether a UserPromptSubmit that fires when a BACKGROUND subagent reports back (the docs say it does) really injects the notice before the main session's next turn was not run; that is the docs' claim only. Live-tested: a -p prompt gets the notice, it repeats, it stops after the ack.
- NOT tested live: RULE 1 / RULE 2 of the canary against really dead hooks (e.g. node missing from the hook environment). Unit-tested only, with synthetic log lines and the real make_brief / check_handin scripts.
- Undetectable, by construction (also written into the audit, section I row 14): every hook dead AND no brief made AND nothing reading the log; a hook that runs and logs but whose output Claude Code drops; a log that cannot be written (`log()` swallows write errors). A deliberately unlaunched brief gives a false alarm after 2 minutes (the message says so).
- The notice only reaches a session that loads `.claude/settings.json` from the main checkout: live in the real session only after the main session merges `agent/rules-fix`. I changed nothing in `C:\Users\bgbg0\Documents\Programming\roadworld\.claude` or `~/.claude`.
- Not done on purpose: no `onFailure: "block"` (deadlock risk, fail-open policy kept); the acknowledgement text is only logged, not checked for truth.
- The real main-checkout `gate.log` (read only) already holds a FAIL-OPEN ("no readable agent transcript", empty agent_type) and 2 "check still failing after 3 blocks" lines, so the first real notice after the merge will list them; they need one acknowledgement.

## Reference files opened

Case: gate notices, ack and canary (not visible work) | Opened: tools/hooks/lib.mjs, tools/hooks/brief_gate.mjs, tools/hooks/handin_gate.mjs, tools/hooks/contract_inject.mjs, tools/hooks/test_hooks.mjs, .claude/settings.json, docs/progress/2026-10-11-hooks/STATUS.md, docs/research/rules-audit-2026-10-11.md

Also opened (web page, not a repo file): https://code.claude.com/docs/en/hooks, fetched twice (offsets 0 and 100000).

## Own fault list and residual

No photos exist for this case (tooling); no renders. FAULTS.md holds three passes; the residual holds only things I cannot fix (false alarm on an unlaunched brief, undetectable total hook death, acknowledgement without action, background-report path not run live).

## Changes (file:function)

- tools/hooks/notice_lib.mjs (new): readGateLog, pendingItems, formatNotice, canaryProblems, canaryReport, isMainCheckout
- tools/hooks/gate_notice.mjs (new, UserPromptSubmit hook), tools/hooks/gate_ack.mjs (new; `node tools/hooks/gate_ack.mjs "<text>"`, 20+ chars, main checkout only)
- .claude/settings.json: UserPromptSubmit entry
- tools/make_brief.mjs: logs a `make_brief` line, prints canary and pending notices on stderr, exit 3 if the canary trips (the brief is still printed)
- tools/check_handin.mjs: prints the canary (problems only) on stderr; exit code unchanged; skipped when `CP_FROM_HOOK=1` (set by tools/hooks/handin_gate.mjs)
- tools/hooks/test_hooks.mjs: 55 new tests (notice, ack, canary rules, the two tools, wiring)
- CLAUDE.md (second gate, item 0), docs/QUEUE.md (machine note), docs/research/rules-audit-2026-10-11.md (section I rows 13 and 14)

Event choice, one sentence: UserPromptSubmit, because the docs say it runs before Claude processes every submitted prompt and when a background subagent reports back, and injects `additionalContext` as a system reminder; Stop was rejected because additionalContext there continues the conversation (forces an extra turn), PostToolUse on Agent fires at launch, not at the end.
Canary rule, one sentence: if the oldest `make_brief` line with no later launch-hook line (`brief` or `inject`) is more than 2 minutes old, or the newest successful `gate_notice` line is older than 3 hours, the hooks are presumed not running.

## Images that carry each claim

| Claim | Image file (opened by me) |
|---|---|
| none (no visual work); evidence is the test run and the live transcript in INDEX.md | none |

## Suite and checks

- `node tools/hooks/test_hooks.mjs`: 110 passed, 0 failed (was 55).
- Live, bundled CLI 2.1.295 (`claude.exe -p --model haiku`, throwaway repo in the scratchpad, nothing installed, no settings changed): run 1 the notice reached the model (it quoted the first line and the 3 categories); run 2 and 2b (after a refused 5-character ack) the notice was still there; the real ack was accepted ("Acknowledged 3 item(s)"); run 3 answered NO NOTICE; the log shows notice x3, ack, then decision none.

## Next step

Main session: merge `agent/rules-fix`, read the existing gate.log lines, run `node tools/hooks/gate_ack.mjs "<what was done>"` once, and check that `gate.log` gets a `gate: notice` line on the next prompt.
