# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\new_agent_worktree.ps1 <name> [-Base <branch>]
# Creates ..\roadworld-wt\<name> (all new worktrees live under ..\roadworld-wt\; old ..\roadworld-wt-* folders stay where they are) on a new branch agent/<name> from the CURRENT tip of the base branch
# (default: the branch checked out in the main checkout). It creates NO docs\reference junction (main session decision, 23 Oct 2026): a link from a worktree into the
# private reference photos is how every photo was wiped on 22 Oct 2026 (a recursive delete of the worktree followed it). Agents open the photos by ABSOLUTE path in the
# main checkout (<main>\docs\reference\..., the brief lists them) and put composites in <main>\docs\reference\_compare\<topic>\.
# To REMOVE a worktree use ONLY: powershell -NoProfile -ExecutionPolicy Bypass -File tools\remove_agent_worktree.ps1 <name> [-DeleteBranch]
# (it also unlinks any old docs\reference junction first and checks the photos are still there). Worktrees made before this change still hold a junction:
# tools\unlink_reference_junctions.ps1 removes it (link only) once the worktree is idle. The PreToolUse hook tools/hooks/delete_guard.mjs is a best-effort net only.
param(
  [Parameter(Mandatory = $true)][string]$Name,
  [string]$Base = ''
)
$ErrorActionPreference = 'Stop'
$common = (git rev-parse --path-format=absolute --git-common-dir).Trim()
$main = (Resolve-Path (Join-Path $common '..')).Path
if (-not $Base) { $Base = (git -C $main rev-parse --abbrev-ref HEAD).Trim() }
$wtRoot = Join-Path (Split-Path $main -Parent) 'roadworld-wt'
if (-not (Test-Path $wtRoot)) { New-Item -ItemType Directory -Path $wtRoot | Out-Null }
$wt = Join-Path $wtRoot $Name
if (Test-Path $wt) { throw "Worktree path already exists: $wt" }
git -C $main worktree add $wt -b ("agent/" + $Name) $Base
if ($LASTEXITCODE -ne 0) { throw "git worktree add failed" }
$refMain = Join-Path $main 'docs\reference'
if (-not (Test-Path $refMain)) { New-Item -ItemType Directory -Path $refMain | Out-Null }
# deliberately NO junction into the worktree (see the header); a fresh checkout has no docs\reference of its own (it is gitignored)
$hash = (Get-FileHash (Join-Path $wt 'CLAUDE.md') -Algorithm SHA1).Hash
$tip = (git -C $wt rev-parse --short HEAD).Trim()
Write-Host "Worktree: $wt"
Write-Host "Branch:   agent/$Name from $Base at $tip"
Write-Host "Reference photos (open by ABSOLUTE path, no link in the worktree): $refMain"
Write-Host "CLAUDE.md sha1: $hash"
Write-Host "Build dir (the only one, gitignored): $(Join-Path $wt 'build')"
Write-Host "Scratch dir (outside the repo): $(Join-Path (Split-Path $main -Parent) ('roadworld-scratch\' + $Name))"
Write-Host "To remove this worktree later: powershell -NoProfile -ExecutionPolicy Bypass -File tools\remove_agent_worktree.ps1 $Name"
Write-Host "Never recursive-delete a worktree by hand: use the removal script above."
