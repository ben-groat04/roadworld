# Usage: powershell -NoProfile -ExecutionPolicy Bypass -File tools\unlink_reference_junctions.ps1 [-Name <worktree name or path> ...] [-All] [-WhatIfOnly] [-Main <main checkout>]
# Removes the docs\reference junction (LINK ONLY, with `cmd /c rmdir`, never a recursive delete) from existing worktrees, so that no worktree holds a path into the
# private reference photos any more (main session decision, 23 Oct 2026; new worktrees are created without one). Run it when the worktree is idle: an agent that is still
# reading photos through docs\reference in its worktree loses that path (briefs give absolute paths into the main checkout, so a fresh agent is unaffected).
# For each registered worktree (other than the main checkout) it: counts the files in <main>\docs\reference; finds links WITHOUT descending into them; unlinks only a
# link whose path ends in docs\reference or whose target lies under <main>\docs\reference (any other link is listed and left alone); counts again after EVERY link and
# stops with ALARM (exit 2) if the number changed. -WhatIfOnly lists what it would do and touches nothing.
param(
  [string[]]$Name = @(),
  [switch]$All,
  [switch]$WhatIfOnly,
  [string]$Main = ''
)
$ErrorActionPreference = 'Stop'
function Fail([string]$m) { Write-Host ("REFUSED: " + $m); exit 1 }
if (-not $Main) {
  $common = (git rev-parse --path-format=absolute --git-common-dir 2>$null)
  if ($LASTEXITCODE -ne 0 -or -not $common) { Fail 'cannot find the main checkout (run from inside the repository, or pass -Main <path>)' }
  $Main = (Resolve-Path (Join-Path $common.Trim() '..')).Path
}
$Main = (Resolve-Path $Main).Path.TrimEnd('\')
if (-not $All -and $Name.Count -eq 0) { Fail 'give -Name <worktree> (repeatable) or -All' }
$refMain = Join-Path $Main 'docs\reference'

function Count-Files([string]$dir) {
  if (-not (Test-Path -LiteralPath $dir)) { return -1 }
  $n = 0
  foreach ($f in [System.IO.Directory]::EnumerateFiles($dir, '*', [System.IO.SearchOption]::AllDirectories)) { $n++ }
  return $n
}
function Find-LinksInto([string]$dir, $list) {
  foreach ($d in [System.IO.Directory]::EnumerateDirectories($dir)) {
    $attr = [System.IO.File]::GetAttributes($d)
    if (($attr -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) { [void]$list.Add($d) }
    elseif ($d -notmatch '[\\/]\.git$') { Find-LinksInto $d $list }
  }
}
function Under([string]$p, [string]$root) { $a = $p.TrimEnd('\').ToLowerInvariant(); $b = $root.TrimEnd('\').ToLowerInvariant(); return ($a -eq $b -or $a.StartsWith($b + '\')) }

$listed = @()
foreach ($l in (git -C $Main worktree list --porcelain)) { if ($l -match '^worktree (.+)$') { $p = ($Matches[1] -replace '/', '\').TrimEnd('\'); if ($p -ine $Main) { $listed += $p } } }
$chosen = @()
if ($All) { $chosen = $listed } else {
  foreach ($n in $Name) {
    $hit = $null
    foreach ($p in $listed) { if ((Split-Path $p -Leaf) -ieq $n -or $p -ieq $n) { $hit = $p } }
    if (-not $hit) { Fail "no registered worktree called '$n' (git worktree list)" }
    $chosen += $hit
  }
}
$before = Count-Files $refMain
Write-Host "Main checkout docs\reference: $before file(s) before. Worktrees to look at: $($chosen.Count)."
$removed = 0; $kept = 0
foreach ($wt in $chosen) {
  if (-not (Test-Path -LiteralPath $wt)) { Write-Host "skip (folder missing): $wt"; continue }
  $links = New-Object System.Collections.ArrayList
  Find-LinksInto $wt $links
  foreach ($p in $links) {
    $target = ''
    try { $target = [string]((Get-Item -LiteralPath $p -Force).Target | Select-Object -First 1) } catch { }
    $isRef = ($p -match '[\\/]docs[\\/]reference$') -or ($target -and (Under $target $refMain))
    if (-not $isRef) { Write-Host "left alone (not a reference link): $p -> $target"; $kept++; continue }
    if ($WhatIfOnly) { Write-Host "would unlink (link only): $p -> $target"; continue }
    Write-Host "Unlinking (link only, target untouched): $p -> $target"
    & cmd.exe /c rmdir "`"$p`"" | Out-Null
    if (Test-Path -LiteralPath $p) { Fail "could not remove the link $p with rmdir; nothing else was touched" }
    $removed++
    $now = Count-Files $refMain
    if ($now -ne $before) { Write-Host "ALARM: docs\reference had $before file(s) and now has $now after unlinking $p. STOP. Do not delete anything else; tell the owner."; exit 2 }
  }
}
$after = Count-Files $refMain
Write-Host "Done: $removed link(s) removed, $kept other link(s) left alone. docs\reference still has $after file(s) (was $before)."
if ($after -ne $before) { exit 2 }
exit 0
