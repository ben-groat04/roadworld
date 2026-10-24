Auditor agent: adcf530413d419374
Verdict: send back
References opened: 10 of 10

(Honest depth: gates9 AUDIT.md and STATUS.md were read at their heads only, since gates13 does not re-audit them; make_brief.mjs, check_handin.mjs, AGENT_CONTRACT.md, CLAUDE.md and the templates were read by grep/head for the parts this round touches; the hooks folder was read in full for brief_gate_impl, suite_record, guard, lib regex sites and state_guard_impl, and by grep or probes for the rest; run_suite.mjs and phase_lib.mjs in full; gate.log read by script.)

## Files opened
docs/progress/gates9/AUDIT.md, docs/progress/gates9/STATUS.md, tools/make_brief.mjs, tools/check_handin.mjs, tools/hooks (all files listed; brief_gate_impl, state_guard_impl, time_guard_impl, suite_record, guard, lib, shell_lex regex sites read), .claude/settings.json, docs/AGENT_CONTRACT.md, docs/templates (all 6), CLAUDE.md, main gate.log (read only, scripted). Also tools/run_suite.mjs, tools/phase_lib.mjs, the audit brief.

## Own fault list (written before opening docs/progress/gates13/STATUS.md and FAULTS.md; probes sent real PreToolUse payloads, agent_id set, cwd = worktree, CP_GATE_STATE_DIR = scratch, to the real hook scripts)

1. **The record signing secret is readable by an agent, so every run_suite record is forgeable.** state_guard allows (exit 0) each of these as a subagent:
   - `cp -r <main>/.claude <dest>` and `cp -r <main>/.claude/. <dest>` (then `cat <dest>/state/ack_secret`);
   - `mv <main>/.claude <dest>`;
   - `cd <main>/.claude && cat st*/ack_se*` (a relative glob after a cd);
   - `tar cf x.tar .claude` from the main checkout.
   Verified by feeding each command to state_guard, delete_guard, git_guard, lock_guard and time_guard: all five exit 0. The direct forms (`cat .../state/ack_secret`, `cd .claude/state && cat ack_secret`, `node -e` naming the path) are refused, so the hole is the parent folder `.claude` and relative globs. With the secret an agent signs any record with signRecord.
2. **`rm -rf <main>/.claude` is allowed by delete_guard and state_guard** (and `mv <main>/.claude ...`): it deletes settings.json (hook wiring), the secret, the lane lock and the gate.log in one command. The guard protects `.claude/state` but not its parent.
3. **`run_suite.mjs --script <any test_*.mjs>` forges a valid suite record.** I made a scratch git repo, wrote `tests/test_fake.mjs` containing only `console.log("5 passed, 0 failed")`, ran `node tools/run_suite.mjs <repo> --script tests/test_fake.mjs --round r1`, and `findSuiteRun` returned `ok: true` for the resulting `suite_node.log`. phase_lib accepts `suite_node.log` alone in place of `suite_gcc.log` (`present.some(n => n !== 'suite_msvc.log')`), so a C++ round can pass the build check with a node script that prints a summary. The make_brief BUILD text advertises `--script`. Nothing ties a `node` suite log to the round's real test binary.
4. **owner.txt identity is not checked on the Bash path.** The Write tool must name the writer ("agent: <me>"), but `echo 'agent: other' > .../unreal_lane.lock/owner.txt` (Bash, no lock yet held) exits 0 from state_guard. An agent can write another agent's id, or take the lock for a name that never releases. Write and Bash disagree.
5. STATUS merge text names the lock `<state>/unreal.lock` (items 5 and the good/bad table); the code and tests use `unreal_lane.lock`. A merger who pastes the text into the contract gives agents a wrong name.
6. tools/hooks/live_launch_categories.json holds 77 rows; the main gate.log now has 81 distinct NO-CONTRACT descriptions. Four later ones ('Disk use survey (read-only)', 'Remove 5 finished worktrees', 'Remove 5 finished worktrees (retry)', 'Commit gate records, remove worktrees') are not in the table or the test, so "every live description mapped" is stale by four.
7. The allow-list refuses form, not substance: `NO-CONTRACT: clerk: <20+ chars>` with a prompt that asks for a review or audit without the words "write AUDIT.md" passes (probe: description "Auditor for x", prompt "NO-CONTRACT: clerk: please audit and review the hand-in thoroughly..." exit 0; "Vet x" under `research` exit 0). That is what the main session decided, so it is a residual to say plainly, not a code fault; STATUS does not say it.

## Faults, ranked worst first

1. Secret readable via `cp -r .claude`, `mv .claude`, `cd .claude && cat st*/ack_se*`, tar (own list 1). State records and suite records are only as good as the secret. Fix: treat `.claude` (the parent) and any path whose resolved real path contains `.claude/state` as state for read, copy, move, archive and delete by agents; resolve globs after `cd`; add REFUSE rows for each form above plus relative-path variants.
2. `--script` forged suite (own list 3). Fix: a node-script record may satisfy the build check only for a round whose brief says the suite is a node suite (BRIEF names it), or the record must carry the repo's own suite file list (a fixed allow-list of existing tests/ and tools/hooks/test_*.mjs files whose content hash is unchanged since main); never a file the round added; C++ rounds require suite_gcc.log. Add REFUSE: a new test_fake.mjs via `--script`.
3. `rm -rf <main>/.claude` and `mv <main>/.claude` allowed (own list 2). Add to delete_guard and state_guard with REFUSE rows.
4. owner.txt spoof on the Bash path (own list 4). Fix: for Bash, accept only redirects whose content names the writer, or refuse Bash writes to owner.txt and require the Write tool; add a REFUSE row and an ALLOW row.
5. Merge text names the lock `unreal.lock` (own list 5); correct to `unreal_lane.lock` in STATUS items 5 and the good/bad table.
6. Live-description table stale by 4 (own list 6): add the four, and make the test read gate.log read-only when present and flag any description not in the table (so the table cannot go stale silently).
7. Residual to disclose in STATUS: category is self-declared (own list 7).

## Builder's measuring tools checked against the data

- All 12 suites run by me one at a time with `timeout 600 node <suite>` and CP_GATE_STATE_DIR = scratch (not through run_suite): test_hooks, test_guards6 to 13, test_gates, test_worktree_tools, test_backup_reference all exit 0. The live gate.log was not written by me.
- Decision 1 (allow-list): my own payloads agree with the tests. Free text refused, unknown category refused (`reviewer`), lowercase `no-contract` refused, indented line refused, clerk + "write AUDIT.md" refused, clerk/merge allowed, Explore exempt, relative AUDIT-OF refused. The builder's 77-row table was checked against gate.log: 4 newer descriptions missing (own list 6).
- Decision 4 (time guard by position): 55 of my own command forms, such as `taskset -c 0 ninja`, nice, env, time, stdbuf, xargs, `bash -c`, `powershell -c`, `cmd /c start`, subshell, `{ }`, `$( )`, backticks, if/for bodies, nohup, sudo, exec, `timeout 0`, `timeout 600m`, `timeout --signal`, `-k`, ninja/make/msbuild/cmake, quoted exe path. All matched expectation except `T=<exe>; $T` and `eval <exe>`, which are the documented unseeable forms.
- Decision 7 (lane lock): in a scratch `.claude/state/unreal_lane.lock/owner.txt` owned by ownerX, a different agent was refused for rmdir, rm -rf, rm owner.txt, rm lock/*, redirect onto owner.txt, mv, `cd lock && rm`, `cd lock/.. && rmdir`, `rm -rf lock/../lock`, sed -i, cp, Remove-Item, `mv state`, `rm -rf state`; the owner's rmdir was allowed; `mkdir` of an existing lock passes the guard (mkdir itself fails). The owner rule works end to end for these forms; the holes are fault 1, 3 and 4 above.
- Decision 8: every `new RegExp(` in tools/hooks and the tools the hooks call was read: brief_gate_impl (constant `file` fragment), handin_gate_impl briefLinesIn (callers pass 'REFS', 'CASE', 'ALLOW-FAIL'), lib.mjs (category constants), gates_lib 46/629/761-782/939 (constants or reEsc), check_handin 339 (key from the fixed METRICS list), phase_lib 89 (constant source). State_guard, delete_guard and lock_guard build regexes only through reEsc/safeRe. No input-derived unescaped RegExp remains. No implicit-regex `.match("string")` on command text either.
- Decision 2 (records): signature, worktree, binary-inside-worktree, hash, filtered and freshness checks read correctly in suite_record.mjs. Forging by a typed or edited log needs the secret (fault 1) or a `--script` run (fault 2); I forged one record with fault 2 end to end in a scratch repo.
- Decision 3: the time guard resolves the root from `cd`, the program path or the run_suite argument (code read, withRoot/rootOfJob); the builder's tests use cwd = main.
- check_handin --phase build was not run end to end by me (no real hand-in of a build phase exists); I checked the code path in phase_lib.buildProblems.

## For the next builder
Fix every item above (contract item 17). Do not edit the gates12 or gates13 AUDIT.md.
