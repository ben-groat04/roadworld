# Faults (no visual work; faults of the mechanism, asked as "what would the main session or an engineer say is wrong")

## Pass 1
1. check_handin run by an agent in a worktree would print the main session's pending notices to the agent, who cannot act on them. FIXED: it prints only the canary problems (test added).
2. `gate_ack.mjs` run from a worktree copy would write into the worktree's own gate.log, which the hooks never read, so the notice would never clear. FIXED: refused with a message unless `.git` is a directory (test added).
3. A newer brief hid an older unlaunched one (first design looked at the newest brief only; the test "still tripped until a launch hook line appears" failed). FIXED: RULE 1 looks at the OLDEST brief with no later launch-hook line.
4. Not in the list: the same hand-in released twice (SubagentHandback and SubagentStop both log) would be reported twice. FIXED: one item per HANDIN_DIR (test added).
5. Not in the list: a heartbeat line on every prompt grows the log. Kept (RULE 2 needs it); about 150 bytes a prompt.

## Pass 2
1. Notice wording: the docs warn imperative text in injected context can trip prompt-injection defences. Checked live: haiku quoted the notice and did not refuse or run anything; the text is factual and names the ack command as the only clearing action.
2. A garbage line, a missing log or an unwritable state dir must not break the hook: tests added (garbage line skipped; no log; `.claude` is a file -> exit 0, silent).
3. `through_line` counted physical lines while readGateLog numbered non-empty lines; both now count non-empty lines (a blank line would have shifted the ack).
4. A failing notice hook writes FAIL-OPEN, which does not count as a heartbeat for RULE 2 (test added) and appears in the next successful notice.

## Pass 3
1. Live run in a throwaway repo: notice, repeat, refused short ack, real ack, silence, as designed.
2. The canary message in make_brief stderr names the benign cause (brief never launched) as well as the hooks-dead one.
3. The real main log (read only) already has a FAIL-OPEN for an agent with empty agent_type and no transcript; the first real notice will include it. Not a code fault; noted in STATUS.

## Residual
- A brief that is made and deliberately not launched trips RULE 1 after 2 minutes (the message names that cause); it cannot be told apart from a dead hook.
- All hooks dead with no brief made and the log unread: undetectable by design.
- The acknowledgement is only a logged sentence; nothing checks that the action was taken.
- UserPromptSubmit on a background subagent's report is per the docs only; not exercised live (headless runs end before background agents return).
