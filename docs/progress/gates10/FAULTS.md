# FAULTS gates10

No-visual round. Method: real hook scripts and functions fed real-shaped JSON, real JSONL transcripts, real git repos and PNGs in a temp folder, plus the full suites. Fail-first against main 5fe652c.

## Pass 1 (own fault list while building)

1. `codeText()` in delete_guard rebuilt the command from lexer words and so lost parentheses and escaped quotes, which broke test_guards6 item 14 and test_guards7 item 1. Fixed: the original text is returned when nothing was dropped.
2. `find ... -exec rm {} \;` was refused for an unlocked pattern because the backslash-semicolon terminator was read as an argument (`rm \`). Found by the new ALLOW table. Fixed in lock_extra (terminators `;`, `+`, `\`, `\;`).
3. The build phase check demanded that BUILD.md list the commit that adds BUILD.md itself, which can never be true. Found by my own good fixture. Fixed: commits that touch only files inside the hand-in folder are not required in the list; every code commit still must be.
4. test_hooks.mjs failed 100+ cases because its fixture checkouts copy a fixed file list and did not know the new modules (image_lib, phase_lib, lock_extra, phase_marks). Fixed in the lists; no expectation changed.
5. `git stash` was run by mistake in the worktree and popped at once; status checked, nothing lost.
6. Shell-script edits ate backslashes and newlines twice (phase_lib line split, one .cjs clobbered by sed); `node --check` caught both, re-applied with the editor.
7. Some "ok" lines of the fail-first run pass on old code only because the old code did nothing (no builder record after a phase). Said plainly in STATUS; not counted as proof.

## Pass 2 (cold read of the finished round, as an auditor would)

1. "Would a builder be refused for a normal reformat?" ALLOW tables cover `find -exec grep`, `rm` of `*.tmp`, `clang-format -i` on unlocked files, `sed -n -e`, `sort -o other`, `robocopy` from a locked file, `patch` and `git apply` of unlocked diffs, `git apply --check`, `fs.openSync(..., 'r')`: all pass.
2. "Does the first-look gate trust the agent's word?" No: the id is compared with the real one, the images are decoded, the order is read from the transcript. It cannot show the agent really looked at them (an agent can open and ignore); the main session's own look stays.
3. "Can a phase agent slip out with CHECKPOINT?" Tested: no.
4. "Is the build-phase suite run really run?" Yes, by the check (the exit code is what counts); tested with a command exiting 0 and one exiting 3.
5. "Would the launch hook refuse a legitimate clerk whose reason mentions audit?" The ALLOW case "Research: Unreal plugin facts" has a reason that mentions an AUDIT.md of another round and stays allowed. A clerk whose job text says "write the audit" would still be refused, deliberately.

## Residual

Only items that cannot be fixed by code:
- A diff piped into `patch` or `git apply` from another command, paths built from variables, and `$(...)` forms other than ls, find, git ls-files, echo and printf cannot be seen statically (stated in the lock_guard and lock_extra headers).
- A first-look agent that opens images without looking at them: the gate shows files opened and a written top 3, not attention; covered by the main session's own look.
- The live phase and first-look agents have not run yet through a real launch (first use is the test).
- The headline reference-photo-copied-into-the-hand-in floor is stated in the item 24 amendment.
