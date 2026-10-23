# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\new_agent_worktree.ps1 <name> [-Base <branch>]
# Creates ..\roadworld-wt\<name> (all new worktrees live under ..\roadworld-wt\; old ..\roadworld-wt-* folders stay where they are) on a new branch agent/<name> from the CURRENT tip of the base branch
# (default: the branch checked out in the main checkout) and links the main checkout's docs\reference into it
# with a directory junction, so agents can open the (gitignored, private) reference photos.
# To REMOVE a worktree use ONLY: powershell -NoProfile -ExecutionPolicy Bypass -File tools\remove_agent_worktree.ps1 <name> [-DeleteBranch]
# (it unlinks the docs\reference junction first and checks the photos are still there). NEVER rm -rf, Remove-Item -Recurse or rmdir /s a worktree: that follows
# the junction and wipes the private reference photos (22 Oct 2026). The PreToolUse hook tools/hooks/delete_guard.mjs refuses such commands.
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
$refWt = Join-Path $wt 'docs\reference'
if (-not (Test-Path $refMain)) { New-Item -ItemType Directory -Path $refMain | Out-Null }
# a fresh checkout has no docs\reference (gitignored); if an EMPTY folder is there, remove it without -Recurse (never recursive-delete anything near the photos)
if (Test-Path $refWt) { [System.IO.Directory]::Delete($refWt) }
New-Item -ItemType Junction -Path $refWt -Target $refMain | Out-Null
$hash = (Get-FileHash (Join-Path $wt 'CLAUDE.md') -Algorithm SHA1).Hash
$tip = (git -C $wt rev-parse --short HEAD).Trim()
Write-Host "Worktree: $wt"
Write-Host "Branch:   agent/$Name from $Base at $tip"
Write-Host "Reference junction: $refWt -> $refMain"
Write-Host "CLAUDE.md sha1: $hash"
Write-Host "Build dir (the only one, gitignored): $(Join-Path $wt 'build')"
Write-Host "Scratch dir (outside the repo): $(Join-Path (Split-Path $main -Parent) ('roadworld-scratch\' + $Name))"
Write-Host "To remove this worktree later: powershell -NoProfile -ExecutionPolicy Bypass -File tools\remove_agent_worktree.ps1 $Name"
Write-Host "NEVER rm -rf / Remove-Item -Recurse / rmdir /s a worktree: the docs\reference junction would take the private photos with it."
