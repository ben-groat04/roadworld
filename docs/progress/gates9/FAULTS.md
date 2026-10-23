# Faults found (gates9), in plain words

No-visual round. Method: payloads fed to the real hook scripts and functions with fake harness JSON, real transcripts, real junctions, real PNGs and real git repos in a temp folder (links unlinked first when removed). Nothing wrote the live gate.log.

## Pass 1 (own fault list, written before the first run of the new suite)

1. A commit that adds the checkpoint picture itself would make CHECKPOINT.md older than the latest commit, voiding every checkpoint (H5 against H4). Fixed: the latest-commit time ignores the whole hand-in folder; pinned in TG9 H5.
2. H6 with a per-agent record would let a second agent take a second checkpoint in the same round. The record is per round (hash of the hand-in folder); pinned (TG9 H6).
3. The H3 bound test with a zero budget did not trigger (`>` against 0); `>=` now, pinned.
4. A wrong expectation of mine in the H6 test (file time must be older than the record time) and one variable reused between H5 cases: test errors, not guard faults; fixed.
5. Regex and path text lost its backslashes when I edited through shell scripts; found by `node --check` and the suite, re-applied with the editor. Nothing shipped with the damage (every suite ran after).

## Pass 2 (what the round-8 auditor or an honest builder would type next)

Allowed, and still allowed by design: `mv new_test.cpp tests/`, `rm tests/*.tmp`, `git clean -n tests`, `python -c "print(open('tests/locked/a.mjs').read())"`, `clang-format tests/locked/a.mjs` without -i, `rm -rf build` in a link-free worktree. Refused: the same with a real locked file as target, every H1 and H7 form, a junction at `build\ref` for `rm -rf build`, `rm -rf *`, `git clean -fdx`, `rmdir /s /q build`, `robocopy empty build /MIR`. No new fault found in this pass.

## Residual (not built, with the reason)

1. A link deeper than 6 levels below a build folder, or in a build tree whose walk takes over 1.5 s, is not found; the scan-bound line goes to gate.log so the main session can see it. Such a link would have to be put there on purpose.
2. Obfuscated writes to a locked test (`cp -t`, `>|`, `curl -o`, `wget -O`, `tar -C`, `unzip -d`, `(Get-Item x).Delete()`, a trailing-dot folder name): decision D of round 8, and the auditor agreed these are not honest-agent routes.
3. `git add --pathspec-from-file`: accepted as pedantic by the round-8 audit.
4. patch, git apply, git stash and git reset can still change a locked test; residual by nature (a builder can always use git).
5. The checkpoint image checks prove a real PNG or JPEG written during the run, not that it shows the right thing; that stays the main session's look.

Closed from round 8: Residual 5 (junctions in build output folders, now found, H3), Residual 4 (the window, now 30 s, H6). Residual 6 wording: `a photo please` IS refused by `needsIsConcrete`, so it is not a gap.
