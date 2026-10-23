# gates6: fix round after the gates5 audit (false blocks, delete and state guard holes, backup tool, BLOCKED evidence)

## Owner's words
11 Oct, after earthworks round 9 was audited and sent back: "think about why round 9 earthworks failed, improve the system"
22 Oct: "when you audit, be strict and ensure EVERYTHING gets addressed in a fix. no excuses for not fixing issues unless its like so pedantic i wouldn't notice or you aren't sure the audit is picking things up right"

## Cases
Engine and tooling work, no renders: HANDIN: no-visual. The evidence is `docs/progress/gates5/AUDIT.md` (items 1 to 32); every payload it marks "must be allowed" or "must be refused" is a test.

## Gates
Why these waivers are legitimate (main session send-back note, 23 Oct 2026; audit item 29): the branch carries the gates2 to gates5 diffs against a merge base that is still the gates1 state of the test files. The scene.mjs try/catch is the gates2 picture-sanity writer (it wraps the stats file write, not an assertion). The four test names were replaced by the gates3 per-item ack rewrite (the new tests cover the same behaviour under new names: "ack with a code", "ack needs 20+ characters", and so on in tools/hooks/test_hooks.mjs).
allow_waiver try_catch_added:scene.mjs
allow_waiver deleted_gate_test:test_hooks.mjs:ack without text: refused, exit 1
allow_waiver deleted_gate_test:test_hooks.mjs:ack with 20+ characters: accepted and counted
allow_waiver deleted_gate_test:test_hooks.mjs:ack logged with text, timestamp, through_line
allow_waiver deleted_gate_test:test_hooks.mjs:acking with nothing pending is allowed
