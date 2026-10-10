<#
.SYNOPSIS
  One command from "UE 5.8 finished installing" to "first screenshots". Safe to re-run.
.DESCRIPTION
  Steps: find-engine, build, environment, textures, level, screenshots.
  Each step records its result in unreal\CityPainter\Saved\first-run-state.json. A re-run skips steps already OK
  (build is also redone if sources/config changed since its OK), and resumes at the first unfinished one.
  When a step actually runs, later steps are invalidated. Stops at the first failure with exit code 1.
  -From <step>   run that step and everything after it, even if OK
  -Only <step>   run just that step (forced)
  -Force         redo every step
  -DryRun        print what would run; change nothing
  -ScriptArgs    passed to the level and screenshots Python scripts (space-joined into one CP_SCRIPT_ARGS value)
  -EngineRoot    passed to every step
  -LocalDdcPath / -MaxParallelActions / -GraphicsAdapter   forwarded to the steps that take them (build: DDC + parallelism;
                 editor steps: DDC + GPU index for -graphicsadapter). Unset = each script's own default (C:\UEDDC, 4, none).
  Each step's log path is printed before the step starts; the build step runs with -Stream, but its output is shown when it ends.
  Exit: 0 all done, 1 a step failed, 2 bad arguments.
.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\First-Run.ps1
#>
[CmdletBinding()]
param(
    [ValidateSet('find-engine', 'build', 'environment', 'textures', 'level', 'screenshots')][string]$From,
    [ValidateSet('find-engine', 'build', 'environment', 'textures', 'level', 'screenshots')][string]$Only,
    [switch]$Force,
    [switch]$DryRun,
    [string[]]$ScriptArgs = @(),
    [string]$EngineRoot,
    [string]$StateFile,
    [int]$ShaderTimeoutMinutes = 120,
    [string]$LocalDdcPath,
    [int]$MaxParallelActions = 0,
    [Nullable[int]]$GraphicsAdapter = $null
)

$ErrorActionPreference = 'Stop'
$tools = $PSScriptRoot
$projectDir = (Resolve-Path (Join-Path $tools '..\CityPainter')).Path
if (-not $StateFile) { $StateFile = Join-Path $projectDir 'Saved\first-run-state.json' }
$shotDir = Join-Path $projectDir 'Saved\Screenshots\look'
$manifest = Join-Path $projectDir 'SourceArt\Textures\PolyHaven\manifest.json'
$scriptArgStr = ($ScriptArgs -join ' ').Trim()
$psPrefix = '-NoProfile -ExecutionPolicy Bypass -File'

$order = @('find-engine', 'build', 'environment', 'textures', 'level', 'screenshots')

function Q($s) { return '"' + $s + '"' }

# Planned log file per step (announced before the step runs; the child script is told to write exactly there).
$runStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
function Get-StepLog($name) {
    $logs = Join-Path $projectDir 'Saved\Logs'
    switch ($name) {
        'build' { return (Join-Path $logs "build-$runStamp.log") }
        'environment' { return (Join-Path $logs "py-report_environment-$runStamp.log") }
        'textures' { return (Join-Path $logs "py-import_textures-$runStamp.log") }
        'level' { return (Join-Path $logs "py-setup_look_test_level-$runStamp.log") }
        'screenshots' { return (Join-Path $logs "py-screenshot_views-$runStamp.log") }
    }
    return ''
}

# Per-step command line (arguments to powershell.exe).
function Get-StepCommand($name) {
    $eng = ''
    if ($EngineRoot) { $eng = ' -EngineRoot ' + (Q $EngineRoot) }
    $sa = ''
    if ($scriptArgStr) { $sa = ' -ScriptArgs ' + (Q $scriptArgStr) }
    $ddc = ''
    if ($LocalDdcPath) { $ddc = ' -LocalDdcPath ' + (Q $LocalDdcPath) }
    $par = ''
    if ($MaxParallelActions -gt 0) { $par = " -MaxParallelActions $MaxParallelActions" }
    $gpu = ''
    if ($null -ne $GraphicsAdapter) { $gpu = " -GraphicsAdapter $GraphicsAdapter" }
    $lp = ''
    if ($name -ne 'find-engine') { $lp = ' -LogPath ' + (Q (Get-StepLog $name)) }
    $ed = "$eng$ddc$gpu$lp"
    switch ($name) {
        'find-engine' { return "$psPrefix $(Q (Join-Path $tools 'Find-Unreal.ps1'))$eng" }
        'build' { return "$psPrefix $(Q (Join-Path $tools 'Build-Editor.ps1')) -Stream$eng$ddc$par$lp" }
        'environment' { return "$psPrefix $(Q (Join-Path $tools 'Run-EditorPython.ps1')) -Script py\report_environment.py -TimeoutMinutes $ShaderTimeoutMinutes$ed" }
        'textures' { return "$psPrefix $(Q (Join-Path $tools 'Run-EditorPython.ps1')) -Script py\import_textures.py -TimeoutMinutes 120$ed" }
        'level' { return "$psPrefix $(Q (Join-Path $tools 'Run-EditorPython.ps1')) -Script py\setup_look_test_level.py -TimeoutMinutes 120$ed$sa" }
        'screenshots' { return "$psPrefix $(Q (Join-Path $tools 'Run-EditorPython.ps1')) -Script py\screenshot_views.py -Render -TimeoutMinutes 120$ed$sa" }
    }
}

function Load-State {
    $h = @{}
    if (Test-Path -LiteralPath $StateFile) {
        try {
            $j = Get-Content -LiteralPath $StateFile -Raw | ConvertFrom-Json
            foreach ($p in $j.PSObject.Properties) {
                $h[$p.Name] = @{ status = [string]$p.Value.status; finishedUtc = [string]$p.Value.finishedUtc; durationSec = [double]$p.Value.durationSec; log = [string]$p.Value.log; exit = [int]$p.Value.exit }
            }
        } catch { Write-Host "State file unreadable, starting fresh: $StateFile" }
    }
    return $h
}
function Save-State($h) {
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $StateFile) | Out-Null
    ($h | ConvertTo-Json -Depth 4) | Set-Content -LiteralPath $StateFile -Encoding UTF8
}

function Newest-SourceTime {
    $newest = [datetime]::MinValue
    $roots = @((Join-Path $projectDir 'Source'), (Join-Path $projectDir 'Plugins'), (Join-Path $projectDir 'Config'), (Join-Path (Split-Path -Parent (Split-Path -Parent $projectDir)) 'sim_core'))
    foreach ($r in $roots) {
        if (-not (Test-Path -LiteralPath $r)) { continue }
        Get-ChildItem -LiteralPath $r -Recurse -File -Include *.cs, *.cpp, *.h, *.hpp, *.ini, *.uplugin -ErrorAction SilentlyContinue |
            Where-Object { $_.FullName -notmatch '\\(Intermediate|Binaries|Saved)\\' } |
            ForEach-Object { if ($_.LastWriteTimeUtc -gt $newest) { $newest = $_.LastWriteTimeUtc } }
    }
    $up = Join-Path $projectDir 'CityPainter.uproject'
    if ((Test-Path -LiteralPath $up) -and (Get-Item $up).LastWriteTimeUtc -gt $newest) { $newest = (Get-Item $up).LastWriteTimeUtc }
    return $newest
}

function Invoke-Step($name) {
    $cmd = Get-StepCommand $name
    $out = Join-Path ([IO.Path]::GetTempPath()) ("cp-firstrun-" + [guid]::NewGuid().ToString('N') + '.txt')
    $err = $out + '.err'
    $p = Start-Process -FilePath 'powershell.exe' -ArgumentList $cmd -PassThru -WindowStyle Hidden -RedirectStandardOutput $out -RedirectStandardError $err
    $null = $p.Handle
    $p.WaitForExit()
    $code = $p.ExitCode
    $lines = @()
    if (Test-Path -LiteralPath $out) { $lines = @(Get-Content -LiteralPath $out); Remove-Item -LiteralPath $out -Force -ErrorAction SilentlyContinue }
    foreach ($l in $lines) { Write-Host "    $l" }
    $errLines = @()
    if (Test-Path -LiteralPath $err) { $errLines = @(Get-Content -LiteralPath $err | Where-Object { $_ -and $_.Trim() }); Remove-Item -LiteralPath $err -Force -ErrorAction SilentlyContinue }
    # A thrown error lands on stderr only; show it on failure so a FAIL is never blank.
    if ($code -ne 0 -and $errLines.Count -gt 0) {
        Write-Host '    stderr:'
        foreach ($l in $errLines) { Write-Host "    ! $l" }
    }
    $log = '-'
    foreach ($l in $lines) {
        if ($l -match 'Full log:\s*(?<p>.+?)\s*$') { $log = $Matches['p'] }
    }
    return @{ exit = $code; log = $log; lines = $lines }
}

$state = Load-State
$stepNames = $order
if ($Only) { $stepNames = @($Only) }

# Which steps are forced?
$forced = @{}
foreach ($n in $order) { $forced[$n] = $false }
if ($Force) { foreach ($n in $order) { $forced[$n] = $true } }
if ($From) { $i = [array]::IndexOf($order, $From); for ($k = $i; $k -lt $order.Count; $k++) { $forced[$order[$k]] = $true } }
if ($Only) { $forced[$Only] = $true }

$newestSrc = Newest-SourceTime
$rows = New-Object System.Collections.ArrayList
$failed = $null
$invalidate = $false
$startAll = Get-Date

Write-Host "First-Run: state file $StateFile"
if (-not $EngineRoot -and -not $env:UE_ROOT) {
    Write-Host 'Note: no -EngineRoot and UE_ROOT is unset, so engine detection will scan drives (slow on a USB disk). Prefer: -EngineRoot D:\UE_5.8'
}
foreach ($name in $stepNames) {
    $reason = ''
    $run = $true
    $entry = $null
    if ($state.ContainsKey($name)) { $entry = $state[$name] }
    if ($forced[$name]) { $reason = 'forced' }
    elseif ($invalidate) { $reason = 'earlier step re-ran' }
    elseif ($entry -and $entry.status -eq 'OK') {
        $run = $false
        if ($name -eq 'build') {
            $fin = [datetime]::MinValue
            if ([datetime]::TryParse($entry.finishedUtc, [Globalization.CultureInfo]::InvariantCulture, [Globalization.DateTimeStyles]::AdjustToUniversal, [ref]$fin) -and $newestSrc -gt $fin) {
                $run = $true; $reason = 'sources changed since last OK build'
            }
        }
    } else { $reason = 'not done yet' }

    if ($name -eq 'textures' -and $run -and -not (Test-Path -LiteralPath $manifest)) {
        Write-Host "[$name] SKIPPED: no manifest at $manifest"
        [void]$rows.Add([pscustomobject]@{ Step = $name; Status = 'SKIPPED(no manifest)'; Seconds = 0; Log = '-' })
        continue
    }
    if (-not $run) {
        Write-Host "[$name] already OK, skipping"
        [void]$rows.Add([pscustomobject]@{ Step = $name; Status = 'SKIP(done)'; Seconds = 0; Log = $entry.log })
        continue
    }
    Write-Host "[$name] RUN ($reason)"
    Write-Host "    powershell.exe $(Get-StepCommand $name)"
    $plannedLog = Get-StepLog $name
    if ($plannedLog) { Write-Host "    log: $plannedLog" }
    if ($DryRun) {
        [void]$rows.Add([pscustomobject]@{ Step = $name; Status = 'DRYRUN'; Seconds = 0; Log = '-' })
        $invalidate = $true
        continue
    }
    $invalidate = $true
    $t0 = Get-Date
    $r = Invoke-Step $name
    $dur = [math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
    $ok = ($r.exit -eq 0)
    $extra = ''
    if ($ok -and $name -eq 'screenshots') {
        $pngs = @()
        if (Test-Path -LiteralPath $shotDir) { $pngs = @(Get-ChildItem -LiteralPath $shotDir -Filter *.png | Where-Object { $_.LastWriteTime -ge $t0.AddSeconds(-2) }) }
        if ($pngs.Count -eq 0) { $ok = $false; $extra = ' (exit 0 but no new PNGs in ' + $shotDir + ')' }
        else {
            Write-Host "    Screenshots written to $shotDir"
            foreach ($f in $pngs) { Write-Host "      $($f.FullName)" }
        }
    }
    $status = 'OK'
    if (-not $ok) { $status = 'FAIL' + $extra }
    # Later steps are stale now: drop their entries.
    $idx = [array]::IndexOf($order, $name)
    for ($k = $idx + 1; $k -lt $order.Count; $k++) { if ($state.ContainsKey($order[$k])) { $state.Remove($order[$k]) } }
    $state[$name] = @{ status = $(if ($ok) { 'OK' } else { 'FAIL' }); finishedUtc = (Get-Date).ToUniversalTime().ToString('o'); durationSec = $dur; log = $r.log; exit = $r.exit }
    Save-State $state
    [void]$rows.Add([pscustomobject]@{ Step = $name; Status = $status; Seconds = $dur; Log = $r.log })
    if (-not $ok) { $failed = $name; break }
}

Write-Host ''
Write-Host 'FIRST-RUN SUMMARY'
($rows | Format-Table -AutoSize -Wrap | Out-String -Width 200).TrimEnd() | Write-Host
Write-Host ("Total {0:N0} s. State: {1}" -f ((Get-Date) - $startAll).TotalSeconds, $StateFile)
if ($failed) {
    Write-Host ''
    Write-Host "FAILED at step '$failed'. Read the log above, fix, then re-run just that step:"
    $extraArgs = ''
    if ($scriptArgStr) { $extraArgs += ' -ScriptArgs ' + (Q $scriptArgStr) }
    if ($EngineRoot) { $extraArgs += ' -EngineRoot ' + (Q $EngineRoot) }
    if ($LocalDdcPath) { $extraArgs += ' -LocalDdcPath ' + (Q $LocalDdcPath) }
    if ($MaxParallelActions -gt 0) { $extraArgs += " -MaxParallelActions $MaxParallelActions" }
    if ($null -ne $GraphicsAdapter) { $extraArgs += " -GraphicsAdapter $GraphicsAdapter" }
    Write-Host "  powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\First-Run.ps1 -Only $failed$extraArgs"
    Write-Host "(or without -Only to resume from there and continue with later steps)"
    exit 1
}
if ($DryRun) { Write-Host 'Dry run: nothing was executed.' }
exit 0
