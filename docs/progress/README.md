# docs/progress: one folder per round of one job

Naming for NEW folders: `<yyyy-mm-dd>-<topic>-r<N>` (example `2026-10-12-roundabout-r1`). Existing folders are NOT renamed (the planning log links to them); older ones use `-rN`, `-playtest` or ordinals (second, third...) instead.

Files a hand-in folder holds (templates in `docs/templates/`, checked by `node tools/check_handin.mjs <folder>`): `STATUS.md` (first heading "Skipped, refused or failed"), `FAULTS.md`, `COMPARE.md`, `INDEX.md`, then `AUDIT.md` written by a different, fresh auditor agent. Nothing is accepted without `AUDIT.md`. Reference-photo composites are NOT committed (they live in `docs/reference/_compare/`).

## Folders on main that have no STATUS and/or no AUDIT/REVIEW (they predate the contract; do not copy them)
- Neither: `2026-10-10-structures-r2`, `2026-10-10-unreal-first`, `2026-10-11-junction-fixes`, `2026-10-11-prototype-ui` (45 screenshots, the UI spec), `2026-10-11-structures-r6`, `2026-10-11-unreal-fifth`, `2026-10-11-unreal-m1-playtest`.
- STATUS only: `2026-10-11-hooks`, `2026-10-11-unreal-m1`.
- AUDIT or REVIEW only: the other `structures`, `unreal-second/third/fourth`, `test-world`, `vegetation-kit`, `junction-markings` (AUDIT.md only, copied from its worktree).
- Both: `2026-10-11-gate-notices`.

## Folders that exist only inside a worktree (not on main yet)
Run `powershell -NoProfile -ExecutionPolicy Bypass -File tools\worktree_status.ps1`: it lists, per worktree, the progress folders not on main and whether each has STATUS ([S]) and AUDIT ([A]). When a branch is merged its folders arrive on main with it.
