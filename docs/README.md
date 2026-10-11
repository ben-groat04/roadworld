# docs/ index

New session: run `powershell -NoProfile -ExecutionPolicy Bypass -File tools\worktree_status.ps1`, then read `HANDOFF.md`, `CLAUDE.md`, `QUEUE.md`. Last revised 11 Oct 2026, evening. Add a line here whenever a file is added.

## Who owns what (one home per fact)
| Document | Owns |
|---|---|
| `../CLAUDE.md` | the main session's working rules (rules only) |
| `AGENT_CONTRACT.md` | the rules every agent follows; pasted unchanged into every brief |
| `HANDOFF.md` | cold start: what the game is, the milestone table (the single copy of state), the pipeline, hooks vs not, the owner's standing rules, where things live |
| `QUEUE.md` | the only work queue: every job and its stage, first actions, questions for the owner |
| `unreal-game-plan.md` | scope of milestones M1-M8, risks (no state) |
| `planning-log.md` | decisions only, append-only (Rounds 1-50) |
| `ACCEPTANCE.md` | log of what the main session opened before accepting a result |

## Entry points and tools
- `HANDOFF.md`, `QUEUE.md`, `ACCEPTANCE.md`, `planning-log.md`, `AGENT_CONTRACT.md` as above. `templates/BRIEF.md`, `templates/STATUS.md`: brief and hand-in templates.
- `tasks/` – prepared task files for briefs (`m3ui.md`, `sliproad.md`, `uelook.md`, `roadtool3-gaps.md`). Feed them to `tools/make_brief.mjs`.
- Scripts in `../tools/`: `worktree_status.ps1` (read-only state of every worktree), `make_brief.mjs`, `check_handin.mjs`, `new_agent_worktree.ps1`, `hooks/` (enforcement, see HANDOFF).

## Decisions and scope
- `road-graph-model.md` – agreed road graph data model. Current.
- `junctions.md` – junction generator interface and design notes. Current.
- `road-marking-rules.md` – UK marking rules, draft from Traffic Signs Manual ch. 5. Reference; numbers tuned by eye.
- `v0-brief.md` – v0 kickoff brief. HISTORICAL.

## Unreal
- `unreal-architecture.md` – the structure reference for the Unreal phase (module layering, sim-to-render contract). Current.
- `unreal-game-plan.md` – the plan for the road builder as an Unreal game (milestones M1-M8, gaps, risks). Current, scope only.
- `unreal-ui-parity.md` – spec of the browser prototype's UI (the target UI); screenshots in `progress/2026-10-11-prototype-ui/`. Current.
- `unreal-photorealism.md`, `performance-plan.md`, `vegetation-and-surface-realism.md` – rendering stack, performance order, vegetation. Reference; they overlap (research/ue58-feature-sweep.md, research/vegetation-plan.md): read the newer research note first.
- `../unreal/tools/README.md`, `../unreal/tools/KNOWN_RISKS.md` – build and editor automation.

## Reference and history
- `reference/` – private real-world photos (gitignored, never commit); each folder has an INDEX; composites in `reference/_compare/<topic>/` (also private). `reference/MATCHED.md` (matched junction reference set) is being written by a research agent and is not on disk yet.
- `repo-health-review.md` – 10 Oct code-structure review; its open code items are in QUEUE "Cleanup".
- `sim-performance-baseline.md`, `windows-setup.md`, `v0-brief.md` – HISTORICAL banner at the top.
- `research/` – dated notes, each ends in a verdict: `ue58-feature-sweep`, `pcg-city-research`, `mesh-terrain-research`, `pre-research` (historical), `vegetation-plan`, `civil-engine-research` (do not buy), `infinigen-preflight` (dropped), `repo-organisation-audit-2026-10-11` (the audit this layout follows). `research/history/` – rules history: `rules-audit-2026-10-11`, `rules-fix-mapping-2026-10-11`, `reference-claims-audit` (a few older files still cite the old paths). Free-asset search notes `research/free-assets-*.md` will be added when the research agents hand in.
- `progress/` – one folder per round; naming and which folders lack STATUS/AUDIT: `progress/README.md`. Unmerged rounds live in worktrees (use `tools/worktree_status.ps1`).

## Reference claims (rule detail)
Rule text is in `../CLAUDE.md` and `AGENT_CONTRACT.md`. Any statement about what the reference photos show names the files actually opened; text in an INDEX is not an opened reference; a score on fewer than two opened references for that view says so; numbers derived from such claims carry the same rule (`research/history/reference-claims-audit.md` shows what went wrong without it).
