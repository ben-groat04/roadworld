# FAULTS gates15 (own fault passes, written before any audit)

## Pass 1 (while building)

1. `node --check` showed SyntaxErrors in `tools/run_suite.mjs` and `state_guard_impl.mjs` (backslashes in regex lines were lost when I pasted them through a shell heredoc). Repaired line by line with the Edit tool; both files check clean. Fixed.
2. Five tests of earlier rounds pinned the OLD lane rule (owner.txt decides): guards13 F (3 rows) and guards14 C (1 row) wrote an owner.txt and expected the second agent to be refused. They now also write the `lane` take line into gate.log, which is what decides. Fixed (not loosened: the refusal rows still refuse).
3. guards13 "no hook builds a RegExp from other text without reEsc/safeRe" flagged `globRe` in lib.mjs. `globRe` IS the guarded compile (try/catch, fail closed), so the test's constant-source list names it. Fixed.
4. guards13 and guards14 live-table and count assertions moved from 81 to 90 rows (the 9 mapped descriptions).
5. test_guards15 first run: 12 FAIL. Causes were all in the test fixtures, none in the tools: relative `--script` paths resolved against the wrong cwd, a fixture repo without `.gitattributes` (the checkout turned LF into CRLF so the byte compare with `git show` differed; the real repo has `* text=auto eol=lf`), a preload fixture that exited before run_suite could refuse, a NODE_OPTIONS fixture pointing at a file URL that does not exist (node failed before the tool ran), and a Write row aimed at main's tools/hooks (refused by the hook-wiring rule, correctly). Fixed in the test.
6. The old-tools run of test_guards15 stopped on a TypeError (`lib.globRe` did not exist yet). The test now catches it, so the fail-first file holds all 162 rows (88 FAIL, 74 ok).

## Pass 2 (the gates14 auditor's probe corpus c1 to c5 plus my c6, re-run after the last code change through the real state_guard.mjs)

- c1 (secret, home, .claude forms): 2 of 110 allowed. `git archive HEAD .claude` (archives committed files only; `.claude/state` is gitignored, so nothing secret is in it; same call as gates14 pass 2) and `cat <state>/ack_se?ret_old` (a `?` glob onto a file called ack_secret_old that my fixture put in the state folder; the real secret is in the home folder since gates14 and nothing in the state folder is a key). Not fixed, said here.
- c3 (suite forms) 0 misses; c5 (secret detection forms) 0 misses; c6 (home bulk forms) 0 misses.
- c4 (owner.txt write forms): 12 forms the probe marked REFUSE are now ALLOWED. This is the ruling: owner.txt is a hint and the log decides, so writing it changes nothing (test E "a hand-written owner.txt naming ag9 does not change who status says holds the lane"). The writes that matter (gate.log) are still refused (E rows).
- One thing the guard cannot do and I did not pretend it does: `NODE_OPTIONS='--import x' node tools/run_suite.mjs` runs `x` inside the run_suite process before run_suite's first line. run_suite then refuses (exit 2, no record), but `x` itself ran. A preload can only hurt by what it does itself, and that is the accepted residual (code run as the same user). The command line is refused by state_guard (test B/E rows) and a record is never written.

## Pass 3

1. The glob crash item: the exact word `[..*Linking` and 10 more bracket shapes go through `judgeCommand`, the glob helpers and the real hook with no throw and no FAIL-OPEN; an invalid pattern now matches everything so the caller REFUSES (before: `safeRe` returned a never-matching regex, so a bad pattern meant ALLOW, which is the fail-open in different clothes; the fail-first file shows the REFUSE row failing on gates14's tools).
2. The live crash came from the main checkout, whose hook code is older than this branch (gates13 already put `safeRe` in `globToRe`); on this branch's base the exact word already did not throw. What was missing here, and is fixed, is the fail-closed meaning.
3. The auditor brief standing item (4b) was added to `docs/templates/AUDIT_BRIEF.md` and rendered by `make_brief --audit`; the brief text has the step number "4b" so the existing step 5 numbering is unchanged.
4. All suites re-run one at a time after the last code commit (see STATUS).

## Residual (accepted by ruling 1, stated plainly)

- Deliberate code run as the same OS user CAN read the signing secret and forge any signed record (suite record, audit record, ack). Hooks run as that user too; a pattern guard cannot stop code that computes the secret path. This is ACCEPTED (main-session ruling of 24 Oct 2026). What this round adds is DETECTION, not prevention: state_guard refuses commands and file writes that name the secret, import a hooks file or preload code outside tools/ and the tests; the auditor brief (item 4b) makes every audit grep the builder's commits, scratch and progress folder for those names and write a "Secret check" section in AUDIT.md.
- The detection is by name. Code that builds the path from pieces and imports nothing from hooks is not caught by the guard; it is only caught if the auditor's grep or reading finds it.
- gates13 audit items 1 and 3 (secret readable, suite record forgeable) are therefore NOT "fixed": they are "residual by ruling 1". run_suite now closes the routes that needed no secret (changed helper, preload, env, dropped-in exe), but a forger who reads the secret can still sign.
- The launch category is self-declared; the main session (no agent id) is not limited by design; the lane lock is a convention enforced by the log, not by the OS.
- Not exercised in a live launch: every check ran as a process against real files in a temp folder with real payload shapes.
