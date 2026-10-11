## Skipped, refused or failed

References opened: 11 of 13 listed

- not opened: https://code.claude.com/docs/en/hooks-guide.md (the fetch tool returned only the first 2 KB of a 60 KB page and I did not read the saved copy; the hooks REFERENCE page, which holds the same event/exit-code rules, was read in full for the relevant sections)
- not opened: https://code.claude.com/docs/en/settings.md (same: first 2 KB only; settings precedence and trust rules NOT verified from the settings page; project `.claude/settings.json` hooks worked in the headless live test)
- NOT tested: the hooks inside the interactive desktop session (only `claude -p` headless runs, CLI 2.1.295 bundled with the desktop app, not on PATH). Whether the desktop app needs a restart or a trust step to load new project hooks is UNKNOWN.
- NOT tested live: exit code 2 from SubagentStop (JSON `decision: block` was used and tested); hook timeout behaviour; whether the 8-block cap applies to SubagentStop (our own cap of 3 stops first); `NO-CONTRACT` launches in a live session (unit-tested only); a builder agent running inside a git worktree (live agents had no worktree; cwd in hook input is the SESSION cwd, so the gate finds the hand-in folder from the brief).
- NOT built, with reason: `.claude/agents/builder.md`. Project hooks in settings.json already apply to every agent type; a custom type would only add frontmatter (model, `background: true`), and nothing forces the main session to pick that type, so it adds no enforcement. Frontmatter hooks also need a workspace-trust step that `-p` does not satisfy.
- Not solved by hooks (see the "Mechanical enforcement" section of `docs/research/rules-audit-2026-10-11.md`): proof that an agent opened a file, the main session opening images, queue, parallelism judgment, agents already running when this is merged.
- `claude` is not on PATH; I used the bundled binary `C:\Users\bgbg0\AppData\Roaming\Claude\claude-code\2.1.295\d2e29494ec83\claude.exe` read-only (nothing installed, no settings changed). The live runs wrote transcripts under `C:\Users\bgbg0\.claude\projects\C--Users-bgbg0-livetest\` (created by the CLI itself) and a throwaway repo `C:\Users\bgbg0\livetest` and `C:\Users\bgbg0\hk` (safe to delete).

## Reference files opened

Case: rules audit and mapping | Opened: docs/research/rules-audit-2026-10-11.md, docs/research/rules-fix-mapping-2026-10-11.md, CLAUDE.md

## Own fault list and residual

No visual work (engine-free tooling), so no FAULTS.md. Residual faults I know of: `NO-CONTRACT` escape hatch; wording check is a string match; `contract_inject` also fires on resumed agents (docs say it de-duplicates); a cap/counter file per agent is never cleaned up; two independent blockers (SubagentHandback and SubagentStop) share one counter, so one hand-in can see up to 3 refusals in total, mixed.

## Changes (file:function)

- tools/hooks/lib.mjs: checkBrief, noContractReason, briefFileOf, firstUserText, log
- tools/hooks/brief_gate.mjs (PreToolUse Agent), contract_inject.mjs (SubagentStart), handin_gate.mjs (SubagentStop and PreToolUse SubagentHandback)
- tools/hooks/test_hooks.mjs: 55 unit tests (run: `node tools/hooks/test_hooks.mjs`)
- .claude/settings.json: wiring; .gitignore: `.claude/state/`
- tools/make_brief.mjs: emits `HANDIN_DIR:` (and `HANDIN: no-visual` with `--no-visual`); docs/templates/BRIEF.md ("time-box" wording removed from the template; placeholder for the HANDIN lines); docs/AGENT_CONTRACT.md: marker line `AGENT-CONTRACT-MARKER: v1`; CLAUDE.md: hooks paragraph
- tools/check_handin.mjs: references resolve as absolute path, relative to main checkout or the hand-in's worktree, or under docs/reference (worktreeRoot, refBases)
- docs/research/rules-audit-2026-10-11.md: section I "Mechanical enforcement"

## Images that carry each claim

None (no visual work).

| Claim | Image file (opened by me) |
|---|---|
| none | none |

## Suite and checks

- `node tools/hooks/test_hooks.mjs`: 55 passed, 0 failed.
- Live (headless CLI 2.1.295): (a) launch without contract refused, message reached the model; (b) background agent with a failing HANDIN_DIR: SubagentStop fired 4 times (the last 3 with `stop_hook_active: true`), blocked 3 times with the failure list, agent answered the reasons, 4th stop allowed, HANDIN_FAILED.txt written; (c)/(d) agent in auto mode (has the SubagentHandback tool): order is PreToolUse(SubagentHandback) then SubagentStop; a SubagentStop block comes AFTER the report was delivered and did not bring the agent back (run c), so the handin gate is also wired on SubagentHandback, which did turn the agent back (run d: refusals alternate between the two events, shared counter, 3 then released).

## Next step

Main session: merge, then run the live test in the section below this file's summary (report message) once in the real session and check `.claude/state/gate.log`.
