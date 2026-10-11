## Skipped, refused or failed

- INCOMPLETE HAND-IN, about 10% done. None of gates A to G is built; no tests, no proof table.
- Refused: the Bash tool was denied by the permission classifier ("Instruction Poisoning") after a mid-task message (arriving as a system-reminder, not in the brief or chat) asked for edits to docs/AGENT_CONTRACT.md item 8 and docs/templates/BRIEF.md. A later read-only Bash call was denied too. I did not work around it. The edits are NOT made. CLAUDE.md untouched.
- Done: worktree created, agent/auditgate merged (test_hooks.mjs 237 passed, 0 failed).
- not opened: the STATUS.md files of the roadtool3, tjunction and roundabout hand-ins (needed for the proof table; not reached).

References opened: 11 of 12 listed

## Reference files opened

Case: Gate design evidence | Opened: C:\Users\bgbg0\Documents\Programming\roadworld\tools\check_handin.mjs, C:\Users\bgbg0\Documents\Programming\roadworld\tools\make_brief.mjs, C:\Users\bgbg0\Documents\Programming\roadworld\docs\AGENT_CONTRACT.md

## Own fault list and residual

Not started (see report).

## Changes (file:function)

Merge of agent/auditgate only.

## Suite and checks

tools/hooks/test_hooks.mjs: 237 passed, 0 failed (after merge).

## Next step

Confirm the item D change, allow Bash, rerun with a fresh builder.
