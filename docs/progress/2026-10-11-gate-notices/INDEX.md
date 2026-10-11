# Render list

No renders (tooling). Evidence list:
- `node tools/hooks/test_hooks.mjs`: 110 passed, 0 failed.
- Live run directory (scratchpad, not committed): a copy of the four hook files, a UserPromptSubmit-only settings.json and a seeded gate.log with one FAIL-OPEN, one released hand-in and one NO-CONTRACT.
- Live results (`claude.exe -p --model haiku`): run 1 notice quoted with 3 categories; run 2 notice; run 2b notice after a refused short ack; ack accepted ("Acknowledged 3 item(s)"); run 3 "NO NOTICE". Log lines: notice (pending 3) x3, ack (through_line 6), notice (decision none, pending 0).
