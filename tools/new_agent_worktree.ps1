# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\new_agent_worktree.ps1 <name> [-Base <branch>]
# Creates ..\roadworld-wt-<name> on a new branch agent/<name> from the CURRENT tip of the base branch
# (default: the branch checked out in the main checkout) and links the main checkout's docs\reference into it
# with a directory junction, so agents can open the (gitignored, private) reference photos.
# To remove a test worktree: delete the junction first with (Get-Item <wt>\docs\reference).Delete(), then git worktree remove. Never Remove-Item -Recurse the junction.
param(
  [Parameter(Mandatory = $true)][string]$Name,
  [string]$Base = ''
)
$ErrorActionPreference = 'Stop'
$common = (git rev-parse --path-format=absolute --git-common-dir).Trim()
$main = (Resolve-Path (Join-Path $common '..')).Path
if (-not $Base) { $Base = (git -C $main rev-parse --abbrev-ref HEAD).Trim() }
$wt = Join-Path (Split-Path $main -Parent) ("roadworld-wt-" + $Name)
if (Test-Path $wt) { throw "Worktree path already exists: $wt" }
git -C $main worktree add $wt -b ("agent/" + $Name) $Base
if ($LASTEXITCODE -ne 0) { throw "git worktree add failed" }
$refMain = Join-Path $main 'docs\reference'
$refWt = Join-Path $wt 'docs\reference'
if (-not (Test-Path $refMain)) { New-Item -ItemType Directory -Path $refMain | Out-Null }
if (Test-Path $refWt) { Remove-Item $refWt -Recurse -Force }
New-Item -ItemType Junction -Path $refWt -Target $refMain | Out-Null
$hash = (Get-FileHash (Join-Path $wt 'CLAUDE.md') -Algorithm SHA1).Hash
$tip = (git -C $wt rev-parse --short HEAD).Trim()
Write-Host "Worktree: $wt"
Write-Host "Branch:   agent/$Name from $Base at $tip"
Write-Host "Reference junction: $refWt -> $refMain"
Write-Host "CLAUDE.md sha1: $hash"
