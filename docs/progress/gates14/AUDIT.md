Auditor agent: a8eaccc5d090f877a
Verdict: send back
References opened: 10 of 10

Audited commit: 854698b (the builder's last commit, on top of 8cdabfe). Honest depth: gates13 AUDIT.md read in full; state_guard_impl, run_suite, suite_record, lane_lock, the lib.mjs secret code and the gates14 STATUS read in full; make_brief, check_handin, the other hooks, templates, contract, CLAUDE.md and settings.json read by grep or by probing; the main gate.log read by script only. Everything ran against scratch copies (CP_GATE_STATE_DIR, scratch HOME and CP_MAIN_ROOT). The live main .claude/state and the real home folder were not touched. Probe harness and case lists: scratchpad/aud14 (probe.mjs, c1 to c5.json, suite.sh).

## Own fault list (written before opening gates14 STATUS.md and FAULTS.md; real PreToolUse payloads through the real state_guard.mjs, agent_id set)

1. **The signing secret is still reachable by any agent, so every signed record is still forgeable.** Moving the file out of the repo closed the copy and glob routes only. An agent writes a script file and runs it, or imports lib.mjs inline. Verified:
   - `node <scratch>/forge.mjs`, where forge.mjs reads `os.homedir()+'/.roadworld'+'-gate/ack_secret'`: ALLOWED.
   - `node -e "import('<wt>/tools/hooks/lib.mjs').then(m=>console.log(m.readSecret()))"`: ALLOWED (only the inline `signRecord` form is refused).
   - The Write tool creating a script that imports lib.mjs and calls signRecord: ALLOWED. Running it (HOME = scratch) printed `secret read: deadbe verifies: true`.
   The guard judges command text only. STATUS "Residual: reachable only by a route that computes a path without naming it" understates this: the route is one Write plus one `node file.mjs`.
2. **Bulk operations over the home folder capture the secret folder and are allowed.** ALLOWED: `cp -r ~ <dest>`, `mv ~ <dest>`, `tar cf x.tar ~`, `zip -r h.zip ~`, `rsync -a ~/ <dest>`, `grep -r . ~`, `Compress-Archive -Path $HOME`, `gci -Recurse $HOME -Filter ack_secret | gc`, `git clone ~/.roadworld-gate <dest>`, `git -C ~/.roadworld-gate init`, `echo ~/.roadworld-gate/ack_secret | xargs cat`. The Grep tool with path = the home folder (pattern `.` or `[a-f0-9]{8}`, glob `ack_*`) and the Glob tool (`**/ack_secret`, `.roadworld-gate/*` under home) are ALLOWED. Only Read of the file is refused. The guard checks "path resolves INTO the secret folder", not "path is an ANCESTOR of it".
3. **Suite records can still be forged three ways; each was verified end to end in a scratch repo with a real signed record and `findSuiteRun` returning ok:true.**
   - (a) Dependency change: an unchanged merge-base `tests/test_real.mjs` imports `./helper.mjs`. Changing only helper.mjs on the branch made run_suite write a signed record for a log reading "999 passed (forged)". The byte check covers the script, not what it imports (every hooks suite imports test_isolation.mjs and lib.mjs).
   - (b) `NODE_OPTIONS="--import file:///<x>/pre.mjs" node tools/run_suite.mjs <wt> --script tests/test_real.mjs`, where pre.mjs prints a summary and exits when argv[1] is the script: record written, ok:true, log "777 passed (child preload)". run_suite passes the agent's environment to the child.
   - (c) gcc mode runs any file at `build/tests/sim_core_tests.exe`; nothing ties it to the CMake build. Read from the code only (no gcc here to build a fake): any executable that prints a doctest line passes.
   The forms the brief names (a new script, an edited script) are refused correctly; the three above are different routes.
4. **The lane lock owner can still be written by Bash forms.** state_guard ALLOWED each of:
   - `cd <state> && echo 'agent: other' > unreal_lane.lock/owner.txt`;
   - `cd <lock> && echo ... > owner.txt` (also `./owner.txt` and `> own*`);
   - `L=<lock>; echo ... > $L/owner.txt`;
   - `perl -pi -e ... <lock>/owner.txt`;
   - `unzip x.zip -d <lock>`;
   - PowerShell `[IO.File]::WriteAllText('<lock>\owner.txt', ...)`.
   The tool itself is bypassed by `node --import ./x.mjs tools/lane_lock.mjs take --agent agX ...` and by `NODE_OPTIONS='--import ./x.mjs'` (a preload can rewrite argv). The other 36 forms I tried were REFUSED: redirect by absolute path, `..` or glob; tee; dd; install; ln -s; truncate; touch; python or node writeFile; Set-Content; Out-File; Add-Content; PowerShell `>`; ni; Copy-Item; cmd /c echo; Write and Edit; wrong `--agent`, `--agent=`, `$(..)` or variable id; cp; mkdir; mv; tar x. The relative-after-cd redirect is the same class the gates13 audit raised for the secret.
5. Table staleness: the live gate.log now holds 3 NO-CONTRACT descriptions not in the table ("Clerk: suitespeed brief and bookkeeping", "M6 research: lighting columns, cover", "Clerk: update pvetrees task, worktree"). The notice code lists them, which is decision 6 working as designed. Mapping them is housekeeping.

What passed my probes (ordinary work stays allowed): 51 of 51 ALLOW rows allowed, including git -C, cmake, ctest, timeout builds, reading docs, grep -r in docs and tools, `find . -name ack_secret`, cp -r docs, tar of tools, rm -rf build, scratch writes, reading ~/.claude/projects tool-results, `cat ~/.gitconfig`, `ls ~`, run_suite, lane_lock status, commit messages and echo text that name the secret folder, and Read, Grep and Glob on repo paths.
Secret refusals that held (about 85 of 105 REFUSE rows): python and node one-liners; split path strings; `~`, `$HOME`, `${HOME}`, `%USERPROFILE%`, `$env:USERPROFILE`, `C:\Users\*`, `\\?\`, 8.3 names, `..`, `//`, case change, char classes, `?` and `*` globs; variables; `cd` or `pushd` then relative or `cat *`; `find ~ -name .. -exec`; `$(find ..)`; base64, xxd, head; redirects; tee; bash -c; eval; junction, mklink, ln -s; robocopy, xcopy, Copy-Item, Compress-Archive of the folder; Write, Edit and Read of the file. Copy, move, archive and recursive delete of `<main>/.claude` also held, including `cd main && cp -r .cl*`, `tar .[c]laude`, `7z`, `rsync`, `rm -rf .claude/*` and `cd .claude && rm -rf *`.

## Faults ranked worst first

1. Own faults 1 and 3: the secret is readable and records forgeable by any script run (a file.mjs, an inline import, a NODE_OPTIONS preload, a changed helper). Files: tools/hooks/state_guard_impl.mjs, tools/hooks/lib.mjs, tools/run_suite.mjs. Fix direction:
   - Refuse a `node` or python command, and a script file it names (read at the time of the command), that mentions lib.mjs, `.roadworld`, `homedir` or `ack_secret`; or make signing need something the agent process lacks (the signer is a hook process, not code an agent can import).
   - run_suite: run the child with a cleaned environment (no NODE_OPTIONS, no `--import`); hash the script's import closure against the merge base; for gcc tie the exe to the build dir (CMake cache or a build stamp).
   - Add REFUSE rows for each probe in c5.json and suite.sh, and a test that signs from a script file with HOME = scratch and expects no valid record.
2. Own fault 2: ancestor-of-secret paths (the home folder as bulk source or search root, the Grep and Glob tools with path = home, git clone and `git -C` into the secret folder, xargs fed the path). Fix: treat the home folder, or any ancestor of `.roadworld-gate`, as secret-bearing for copy, move, archive, recursive grep, find -exec, Compress-Archive, gci -Recurse, and the Grep and Glob tools.
3. Own fault 4: lane lock owner writes by cd-relative redirect, variable path, perl -pi, unzip -d, [IO.File]::WriteAllText, and `node --import` or NODE_OPTIONS around lane_lock. Fix: apply the cd tracking used for the secret to every write verb aimed at the lock folder; refuse a `node` command naming lane_lock.mjs with `--import`, `--require`, `-r` or a NODE_OPTIONS assignment; refuse in-place editors and extractors (perl -pi, unzip, 7z x, Expand-Archive) and .NET writers aimed at the folder.
4. STATUS residual line misstates the secret exposure (own fault 1) and is silent on the suite routes (fault 3). Rewrite it plainly. The "Prior audit items" table marks gates13 audit items 1 and 3 "fixed" although a script-file read of the secret and the three forgeries still work: re-open both.
5. Three live NO-CONTRACT descriptions unmapped (own fault 5): add them, keep the notice.

## Builder's measuring tools checked against the data

- All 13 suites run by me one at a time with `timeout 600 node <suite>` and CP_GATE_STATE_DIR in scratch: test_hooks 419, guards6 32, 7 28, 8 56, 9 54, 10 107, 11 170, 12 95, 13 139, 14 97, test_gates 514, test_worktree_tools 27, test_backup_reference 56. All exit 0 and agree with the STATUS counts. The live gate.log was not written by me.
- test_guards14 A, B, C take their expected values from the gates13 audit's own probes and the main-session decisions; they pass and test what they say for the listed forms. None of my forgeries or bypasses was in their tables. Test B checks script bytes only, never imports or environment; test C has no cd-relative or variable form.
- STATUS says the suites were not run through run_suite.mjs because every script changed this round: honest, and consistent with decision 2 (so this round has no records and would fail `check_handin --phase build`; it is not a phased round).
- Decision 5: grep finds `unreal.lock` only in the STATUS table row that describes the fix. Decision 6: gate_notice lists unmapped launches (checked against the live log with the notice code: 3 listed). Decision 7: the self-declared text is plain in STATUS and in merge text 1. The contract and CLAUDE.md lock lines are given as merge text, not applied (no main edits by brief).
- Secret migration checked on scratch only: ensureSecret moves a legacy `<state>/ack_secret` to `<home>/.roadworld-gate` (test A rows prove it); not run against real folders.

## For the next builder
Fix every item above (contract item 17). Do not edit the gates12, gates13 or gates14 AUDIT.md.
