# Suite speed round 1: the sim_core suite finishes well inside its time limit

## Owner's words
23 Oct 2026: "these are some slow agents!!"

## Scope
The full sim_core suite (`tests/sim_core_tests.exe`, about 470 test cases) runs as one process and does not finish in 900 s when two other builds are running (QUEUE 23 Oct: slowest case "vegetation: incremental update equals a full rebuild after scripted edits", 70 s; world_cache cases 10-12 s). Every round waits on it several times. This round makes the suite run in parallel and reports the slow cases. It is build and test plumbing only: no geometry, no assertion changes.

## Cases
Engine and tooling work, no renders: HANDIN: no-visual.

Files to open, all of them (M = 6):
- C:\Users\bgbg0\Documents\Programming\roadworld\CMakeLists.txt
- C:\Users\bgbg0\Documents\Programming\roadworld\tests\CMakeLists.txt
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\AGENT_CONTRACT.md (items 8, 25 and 26)
- C:\Users\bgbg0\Documents\Programming\roadworld\tools\check_handin.mjs (how it reads a suite log: the single-exe output it expects must keep working)
- C:\Users\bgbg0\Documents\Programming\roadworld\docs\QUEUE.md (the suite follow-up in the 23 Oct 21:40 section)
- C:\Users\bgbg0\Documents\Programming\roadworld\third_party\doctest (the doctest.h options for filters, --durations, --test-case and --source-file)

## Decisions by the main session (contract item 19)
1. **Parallel ctest shards.** Register the doctest cases as several ctest tests (one per test source file, or grouped files, using doctest's `--source-file=` filter on the same exe) so `ctest --test-dir build -j 4` runs them in parallel. The single-exe run (`tests/sim_core_tests.exe` with no arguments) stays exactly as it is, with the same output format, so every existing tool and hand-in check keeps working.
2. **Coverage proof.** A check (a CMake-registered test or a node script under tests/) shows the shards together run every test case the plain exe runs: the same count, with none run twice and none dropped. When a new test file is added without a shard, the check fails.
3. **Targets.** On this machine, the parallel suite takes 300 s or less of wall-clock time when idle, and 600 s or less while one other gcc build runs. Measure both and record the machine load (free RAM, other processes). If a target is missed, name the shard and the cases that cause it.
4. **Slow-case report.** Run the plain exe once with `--durations=yes` and write SLOW.md: the 25 slowest cases with seconds, file and line. For each case over 20 s, write what makes it slow (input size, repeated full rebuilds, a sleep) and a proposed fix that keeps every assertion.
5. **Shrinking cases.** Only in test files that no running branch has changed. Before editing a test file, check `git diff --name-only main...<branch>` for each of agent/m2fix5c, agent/grade-r14, agent/tjunction7, agent/m3ui-r3 and agent/gates13; skip any file listed. In the files you may edit, a case may get a smaller input only where its assertion does not depend on size, and no assertion may be removed, loosened, retargeted or guarded (the waiver guard applies; there are no allow_waiver lines for this round). Scaling and timing cases keep their sizes; give them the doctest/ctest label `perf` and run them as their own shard.
6. **Docs.** One line in CLAUDE.md (Layout, `tests/`) and one in contract item 26 giving the parallel suite command. Write both as proposed text in STATUS.md under "Merge text"; do not edit those two files yourself.
7. **Checks.** gcc while iterating; one clean gcc build and one MSVC build of the final state, each running both the plain exe and the parallel ctest; save every log in the hand-in folder (suite_gcc.txt, suite_msvc.txt, ctest_gcc.txt, ctest_msvc.txt, durations.txt). Every run under `timeout 900`.

## Faults
1. The full suite does not finish in 900 s under load; there is no parallel run.
2. There is no list of slow cases with causes.

## Finish line
Both compilers run the parallel suite inside the targets, with a coverage check that proves no case is dropped, and SLOW.md gives a cause for every case over 20 s. Stop rule: the self-audit residual holds only unfixable items, or after 3 passes.

## Launched
- suitespeed builder (this round): parallel ctest shards, coverage check, slow-case report. It touches only tests/CMakeLists.txt, test files no running branch has changed, and its hand-in folder.
## Waiting
- none
