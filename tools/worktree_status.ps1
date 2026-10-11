# Read-only. Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\worktree_status.ps1
# Prints one line per git worktree: branch, tip, age, ahead/behind main, merged into main, uncommitted files,
# build* dirs, and for each progress folder that exists only in that worktree (not on main) whether it has
# STATUS.md / AUDIT.md. Then lists scratch/build folders next to the repo. Changes nothing.
$ErrorActionPreference = 'Continue'
$common = (git rev-parse --path-format=absolute --git-common-dir).Trim()
$main = (Resolve-Path (Join-Path $common '..')).Path
$mainBranch = (git -C $main rev-parse --abbrev-ref HEAD).Trim()
Write-Host "Main checkout: $main  branch $mainBranch  tip $((git -C $main rev-parse --short HEAD).Trim())"
Write-Host ""

$wts = @(); $cur = @{}
foreach ($l in (git -C $main worktree list --porcelain)) {
  if ($l -match '^worktree (.+)$') { if ($cur.Count) { $wts += [pscustomobject]$cur }; $cur = @{ Path = $Matches[1]; Branch = '(detached)' } }
  elseif ($l -match '^branch refs/heads/(.+)$') { $cur.Branch = $Matches[1] }
}
if ($cur.Count) { $wts += [pscustomobject]$cur }

$mainProg = @()
if (Test-Path (Join-Path $main 'docs\progress')) { $mainProg = (Get-ChildItem (Join-Path $main 'docs\progress') -Directory).Name }

$rows = foreach ($w in $wts) {
  $p = $w.Path -replace '/', '\'
  if (-not (Test-Path $p)) { [pscustomobject]@{ Dir = (Split-Path $p -Leaf); Branch = $w.Branch; Tip = 'MISSING'; Age = ''; AheadBehind = ''; Merged = ''; Dirty = ''; Builds = ''; Progress = '' }; continue }
  $tip = (git -C $p rev-parse --short HEAD).Trim()
  $age = (git -C $p log -1 --format=%cr).Trim()
  $ab = (git -C $p rev-list --left-right --count "$mainBranch...HEAD").Trim() -split '\s+'
  $merged = 'no'; git -C $p merge-base --is-ancestor HEAD $mainBranch 2>$null; if ($LASTEXITCODE -eq 0) { $merged = 'YES' }
  $dirty = @(git -C $p status --porcelain).Count
  $builds = @(Get-ChildItem $p -Directory -Filter 'build*' -ErrorAction SilentlyContinue).Count
  $prog = @()
  $pd = Join-Path $p 'docs\progress'
  if (Test-Path $pd) {
    foreach ($d in (Get-ChildItem $pd -Directory)) {
      if ($mainProg -contains $d.Name -and $p -ne $main) { continue }
      $s = if (Test-Path (Join-Path $d.FullName 'STATUS.md')) { 'S' } else { '-' }
      $a = if (Test-Path (Join-Path $d.FullName 'AUDIT.md')) { 'A' } else { '-' }
      if ($p -eq $main) { continue }
      $prog += "$($d.Name)[$s$a]"
    }
  }
  [pscustomobject]@{ Dir = (Split-Path $p -Leaf); Branch = $w.Branch; Tip = $tip; Age = $age; AheadBehind = "+$($ab[1]) -$($ab[0])"; Merged = $merged; Dirty = $dirty; Builds = $builds; Progress = ($prog -join ' ') }
}
$rows | Format-Table Dir, Branch, Tip, Age, AheadBehind, Merged, Dirty, Builds -AutoSize | Out-String -Width 200 | Write-Host
Write-Host "Progress folders not on main ([S]TATUS [A]UDIT present, - missing):"
foreach ($r in $rows) { if ($r.Progress) { Write-Host "  $($r.Dir): $($r.Progress)" } }
Write-Host ""
$mb = @(Get-ChildItem $main -Directory -Filter 'build*' -ErrorAction SilentlyContinue)
Write-Host "Main checkout build* dirs: $($mb.Count)"
Write-Host ""
Write-Host "Folders next to the repo (scratch or build trees; owner decides deletion):"
$parent = Split-Path $main -Parent
Get-ChildItem $parent -Directory -Filter 'roadworld-*' -ErrorAction SilentlyContinue | Where-Object { $_.Name -notlike 'roadworld-wt-*' -and $_.Name -ne 'roadworld-wt' } | ForEach-Object { Write-Host "  $($_.FullName)" }
foreach ($x in @("$env:USERPROFILE\cp_scratch", "$env:USERPROFILE\livetest", "$env:USERPROFILE\hk")) { if (Test-Path $x) { Write-Host "  $x" } }
Get-ChildItem $parent -Directory -Filter 'roadworld-wt-*' | Where-Object { -not (Test-Path (Join-Path $_.FullName '.git')) } | ForEach-Object { Write-Host "  orphan (not a worktree): $($_.FullName)" }
