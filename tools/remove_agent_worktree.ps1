# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\remove_agent_worktree.ps1 <name-or-path> [-DeleteBranch] [-Force] [-Main <main checkout>]
# The ONLY supported way to remove an agent worktree. Why: every worktree has docs\reference, a directory junction to the private reference photos in the
# main checkout. A recursive delete (rm -rf, Remove-Item -Recurse, rmdir /s) follows that junction and wipes the photos (22 Oct 2026: every reference photo was lost
# this way). This script (1) counts the files in the main checkout's docs\reference, (2) finds every junction / symlink inside the worktree WITHOUT descending into
# it and removes the LINK ONLY with `cmd /c rmdir` (no /s), (3) counts again and stops if the number changed, (4) only then runs `git worktree remove`
# (without --force: a dirty worktree is refused; pass -Force only for a worktree whose changes you have looked at). The branch is deleted only with -DeleteBranch,
# and then with `git branch -d` (merged branches only), never -D. Any doubt = refuse and say why.
# -Name accepts: a worktree folder name (resolved under ..\roadworld-wt\ and, for old ones, ..\roadworld-wt-<name>) or a full path.
param(
  [Parameter(Mandatory = $true)][string]$Name,
  [switch]$DeleteBranch,
  [switch]$Force,
  [string]$Main = ''
)
$ErrorActionPreference = 'Stop'
function Fail([string]$m) { Write-Host ("REFUSED: " + $m); exit 1 }

if (-not $Main) {
  $common = (git rev-parse --path-format=absolute --git-common-dir 2>$null)
  if ($LASTEXITCODE -ne 0 -or -not $common) { Fail 'cannot find the main checkout (run from inside the repository, or pass -Main <path>)' }
  $Main = (Resolve-Path (Join-Path $common.Trim() '..')).Path
}
$Main = (Resolve-Path $Main).Path
$parent = Split-Path $Main -Parent
$candidates = @()
if (Test-Path -LiteralPath $Name) { $candidates += (Resolve-Path -LiteralPath $Name).Path }
$candidates += (Join-Path (Join-Path $parent 'roadworld-wt') $Name)
$candidates += (Join-Path $parent ('roadworld-wt-' + $Name))
$wt = $null
foreach ($c in $candidates) { if ($c -and (Test-Path -LiteralPath $c)) { $wt = (Resolve-Path -LiteralPath $c).Path; break } }
if (-not $wt) { Fail ("no such worktree folder: tried " + ($candidates -join ', ')) }
$wt = $wt.TrimEnd('\')
if ($wt -ieq $Main.TrimEnd('\')) { Fail 'that is the main checkout, not an agent worktree' }

# it must be a registered worktree of THIS repository, and not the main one
$listed = @()
foreach ($l in (git -C $Main worktree list --porcelain)) { if ($l -match '^worktree (.+)$') { $listed += ($Matches[1] -replace '/', '\') } }
$isListed = $false
foreach ($p in $listed) { if ($p.TrimEnd('\') -ieq $wt) { $isListed = $true } }
if (-not $isListed) { Fail ("$wt is not a registered worktree of $Main (git worktree list): not touching it") }
$leaf = Split-Path $wt -Leaf
if ($wt -notmatch '[\\/]roadworld-wt[\\/][^\\/]+$' -and $wt -notmatch '[\\/]roadworld-wt-[^\\/]+$' -and -not $env:CP_REMOVE_ANY_WORKTREE) { Fail "$wt is not under ..\roadworld-wt\ or named ..\roadworld-wt-<name>: not an agent worktree path" }

function Count-Files([string]$dir) {
  if (-not (Test-Path -LiteralPath $dir)) { return -1 }
  $n = 0
  foreach ($f in [System.IO.Directory]::EnumerateFiles($dir, '*', [System.IO.SearchOption]::AllDirectories)) { $n++ }
  return $n
}
# walk WITHOUT following links: returns the reparse points (links) found below $dir
function Find-Links([string]$dir) {
  $out = @()
  foreach ($d in [System.IO.Directory]::EnumerateDirectories($dir)) {
    $attr = [System.IO.File]::GetAttributes($d)
    if (($attr -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { $out += ,@('dir', $d) }
    elseif ($d -notmatch '[\\/]\.git$') { $out += Find-Links $d }
  }
  foreach ($f in [System.IO.Directory]::EnumerateFiles($dir)) {
    if (([System.IO.File]::GetAttributes($f) -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { $out += ,@('file', $f) }
  }
  return $out
}

$refMain = Join-Path $Main 'docs\reference'
$before = Count-Files $refMain
Write-Host "Main checkout docs\reference: $before file(s) before."
$links = @(Find-Links $wt)
foreach ($lk in $links) {
  $kind = $lk[0]; $p = $lk[1]
  Write-Host "Unlinking $kind link (link only, target untouched): $p"
  if ($kind -eq 'dir') {
    & cmd.exe /c rmdir "`"$p`"" | Out-Null
    if (Test-Path -LiteralPath $p) { Fail "could not remove the link $p with rmdir; nothing else was touched" }
  } else { [System.IO.File]::Delete($p) }
  $now = Count-Files $refMain
  if ($now -ne $before) { Write-Host "ALARM: the main checkout's docs\reference had $before file(s) and now has $now after unlinking $p. STOP. Do not delete anything else; tell the owner."; exit 2 }
}
$after = Count-Files $refMain
if ($after -ne $before) { Write-Host "ALARM: docs\reference file count changed ($before -> $after). STOP."; exit 2 }
# the worktree must now hold no link at all
$left = @(Find-Links $wt)
if ($left.Count -gt 0) { Fail ("links still present: " + (($left | ForEach-Object { $_[1] }) -join ', ')) }

$gitArgs = @('-C', $Main, 'worktree', 'remove')
if ($Force) { $gitArgs += '--force' }
$gitArgs += $wt
& git @gitArgs
if ($LASTEXITCODE -ne 0) { Fail "git worktree remove refused (uncommitted or untracked changes? look at them first; -Force only if they are known to be disposable). The links were already removed; the files are untouched." }
$final = Count-Files $refMain
if ($final -ne $before) { Write-Host "ALARM: docs\reference file count changed after git worktree remove ($before -> $final). STOP."; exit 2 }
Write-Host "Removed worktree $wt. docs\reference still has $final file(s) (unchanged)."
if ($DeleteBranch) {
  $br = 'agent/' + $leaf
  & git -C $Main branch -d $br
  if ($LASTEXITCODE -ne 0) { Write-Host "Branch $br kept (git branch -d refused: not merged). Merge it first, or delete it by hand if the owner agrees." }
}
exit 0
