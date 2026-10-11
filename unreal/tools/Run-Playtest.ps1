<#
.SYNOPSIS
  Runs the scripted City Painter playtest (camera, picking, HUD) in a real game session and prints a summary.
.DESCRIPTION
  Launches UnrealEditor.exe <uproject> /Game/CityPainter/Maps/L_Sandbox -game -CPPlaytest ...: the game module's
  UCPPlaytestDriver (unreal/CityPainter/Source/CityPainter/Playtest/) drives the real controller and pawn through Slate
  input events (right-drag rotate, middle-drag pan, wheel, WASD, arrows, Q/E, Shift), takes screenshots, checks the numbers
  against the prototype's orbit camera and the sim, writes results.json and quits. A fixed 30 fps time step
  (-benchmark -fps=30) makes the numbers repeatable; frame times are measured on the wall clock.
  Output directory (default unreal\CityPainter\Saved\Playtest\latest): results.json, done.txt, 00_start.png ... .
  Needs the editor module built first (Build-Editor.ps1). The machine's mouse is not touched (events are synthesised
  inside the game). Exit code: 0 all checks passed, 1 some failed, 2 setup problem, 124 timeout, 3 crash / no results.
  Extend the test in Source\CityPainter\Playtest\CPPlaytestScript.cpp (one function per scenario).
.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\Run-Playtest.ps1
  powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\Run-Playtest.ps1 -Out C:\temp\pt -ResX 1280 -ResY 720
#>
[CmdletBinding()]
param(
    [string]$Out,
    [int]$ResX = 1600,
    [int]$ResY = 900,
    [string]$Map = '/Game/CityPainter/Maps/L_Sandbox',
    [string]$EngineRoot,
    [int]$TimeoutMinutes = 25,
    [string]$LocalDdcPath = 'C:\UEDDC',
    [Nullable[int]]$GraphicsAdapter = $null,
    [string[]]$ExtraArgs = @()
)
$ErrorActionPreference = 'Stop'

try { $ue = & (Join-Path $PSScriptRoot 'Find-Unreal.ps1') -EngineRoot $EngineRoot -Quiet }
catch { Write-Host "Cannot find Unreal: $_"; exit 2 }

$projectDir = Resolve-Path (Join-Path $PSScriptRoot '..\CityPainter')
$uproject = Join-Path $projectDir 'CityPainter.uproject'
if (-not $Out) { $Out = Join-Path $projectDir 'Saved\Playtest\latest' }
$Out = [IO.Path]::GetFullPath($Out)
if (Test-Path -LiteralPath $Out) { Remove-Item -LiteralPath $Out -Recurse -Force }
New-Item -ItemType Directory -Force -Path $Out | Out-Null
if (-not [Environment]::GetEnvironmentVariable('UE-LocalDataCachePath')) { ${env:UE-LocalDataCachePath} = $LocalDdcPath }

$editor = $ue.EditorExe
if (-not (Test-Path -LiteralPath $editor)) { Write-Host "Missing $editor"; exit 2 }
$log = Join-Path $Out 'game.log'
$args = @(
    ('"' + $uproject + '"'), $Map, '-game', '-CPPlaytest', ('-CPPlaytestOut="' + $Out + '"'),
    '-benchmark', '-fps=30', '-windowed', '-ForceRes', "-ResX=$ResX", "-ResY=$ResY", '-RenderOffscreen',
    '-unattended', '-nosplash', '-NoSound', '-nosourcecontrol', '-NoLoadingScreen', '-stdout', '-FullStdOutLogOutput',
    ('-abslog="' + $log + '"')
)
if ($null -ne $GraphicsAdapter) { $args += "-graphicsadapter=$GraphicsAdapter" }
$args += $ExtraArgs

Write-Host "Playtest: $Map at ${ResX}x${ResY}, output $Out"
$p = Start-Process -FilePath $editor -ArgumentList ($args -join ' ') -PassThru -WindowStyle Hidden
$null = $p.Handle
$sw = [Diagnostics.Stopwatch]::StartNew()
$timedOut = $false
if (-not $p.WaitForExit($TimeoutMinutes * 60 * 1000)) {
    $timedOut = $true
    & taskkill.exe /PID $p.Id /T /F | Out-Null     # only the process this script started
}
$sw.Stop()
$resultsPath = Join-Path $Out 'results.json'
if ($timedOut) { Write-Host "TIMEOUT after $TimeoutMinutes min; log: $log"; exit 124 }
if (-not (Test-Path -LiteralPath $resultsPath)) {
    Write-Host "No results.json (exit code $($p.ExitCode)); the game crashed or the driver did not start. Log: $log"
    if (Test-Path -LiteralPath $log) { Get-Content $log -Tail 25 | ForEach-Object { Write-Host "  $_" } }
    exit 3
}
$r = Get-Content -Raw -LiteralPath $resultsPath | ConvertFrom-Json
Write-Host ("{0} checks, {1} failed, {2:N0} s wall" -f $r.checks_total, $r.checks_failed, $sw.Elapsed.TotalSeconds)
foreach ($c in $r.checks) {
    $tag = if ($c.pass) { 'PASS' } else { 'FAIL' }
    Write-Host ("  [{0}] {1}: {2}" -f $tag, $c.control, $c.observed)
}
foreach ($m in $r.metrics.PSObject.Properties) { Write-Host ("  metric {0} = {1:N2}" -f $m.Name, $m.Value) }
Write-Host "Results: $resultsPath ; screenshots: $($r.screenshots.Count) in $Out"
if ($r.checks_failed -gt 0) { exit 1 }
exit 0
