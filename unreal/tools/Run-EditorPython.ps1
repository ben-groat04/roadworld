<#
.SYNOPSIS
  Run a Python script inside an unattended Unreal editor and print a compact summary of the log.
.DESCRIPTION
  Launches UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=<script> -unattended -nosplash ...
    default:   -nullrhi  (no rendering; fine for level setup, actor placement, reports)
    -Render:   no -nullrhi; adds -windowed -ForceRes -ResX/-ResY and -RenderOffscreen (screenshots)
  The editor log goes to Saved\Logs\py-<name>-<timestamp>.log (via -abslog). The summary shows
  LogPython lines (capped), Python tracebacks/errors, module-load failures, crash markers and the
  script's own result marker ("CP_PY_RESULT: OK" / "CP_PY_RESULT: FAIL ...", printed by py\cp_common.py).
  Scripts get arguments through the environment variable CP_SCRIPT_ARGS (space separated) and find
  their helper modules through CP_PY_DIR. Scripts quit the editor themselves (cp_common.finish);
  this runner kills the process tree after -TimeoutMinutes as a backstop.
  If the environment variable UE-LocalDataCachePath is unset it is set for this process to -LocalDdcPath (default C:\UEDDC).
  -GraphicsAdapter N appends -graphicsadapter=N (pick the RTX 4060 when the integrated Radeon is also present).
  Exit code: 0 = script reported OK, 1 = script/editor failure, 2 = setup problem, 124 = timeout.
.EXAMPLE
  .\Run-EditorPython.ps1 -Script py\report_environment.py
  .\Run-EditorPython.ps1 -Script py\screenshot_views.py -Render
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Script,
    [string[]]$ScriptArgs = @(),
    [switch]$Render,
    [int]$ResX = 1920,
    [int]$ResY = 1080,
    [string]$EngineRoot,
    [int]$TimeoutMinutes = 60,
    [string]$LocalDdcPath = 'C:\UEDDC',
    [string]$LogPath,        # default Saved\Logs\py-<name>-<timestamp>.log; First-Run.ps1 sets it so it can announce the path first
    [Nullable[int]]$GraphicsAdapter = $null,
    [int]$MaxPythonLines = 80,
    [string[]]$ExtraEditorArgs = @()
)

$ErrorActionPreference = 'Stop'

try {
    $ue = & (Join-Path $PSScriptRoot 'Find-Unreal.ps1') -EngineRoot $EngineRoot -Quiet
} catch {
    Write-Host $_.Exception.Message
    exit 2
}

# Derived-data cache: keep it off the (slow) engine drive unless the user already chose one.
if (-not ${env:UE-LocalDataCachePath}) {
    New-Item -ItemType Directory -Force -Path $LocalDdcPath | Out-Null
    ${env:UE-LocalDataCachePath} = $LocalDdcPath
}

$projectDir = (Resolve-Path (Join-Path $PSScriptRoot '..\CityPainter')).Path
$uproject = Join-Path $projectDir 'CityPainter.uproject'
$pyDir = Join-Path $PSScriptRoot 'py'

# Resolve the script: as given, relative to tools\, or relative to tools\py\.
$scriptPath = $null
foreach ($cand in @($Script, (Join-Path $PSScriptRoot $Script), (Join-Path $pyDir $Script))) {
    if (Test-Path -LiteralPath $cand -PathType Leaf) { $scriptPath = (Resolve-Path -LiteralPath $cand).Path; break }
}
if (-not $scriptPath) { Write-Host "Python script not found: $Script"; exit 2 }

# The game module must already be built, otherwise an unattended editor cannot rebuild it and aborts.
$dll = Join-Path $projectDir 'Binaries\Win64\UnrealEditor-CityPainter.dll'
$dll2 = Join-Path $projectDir 'Plugins\CityPainterCore\Binaries\Win64\UnrealEditor-CityPainterSim.dll'
if (-not ((Test-Path -LiteralPath $dll) -or (Test-Path -LiteralPath $dll2))) {
    Write-Host 'WARNING: no built project binaries found. Run unreal\tools\Build-Editor.ps1 first (the unattended editor cannot rebuild modules).'
}

$logDir = Join-Path $projectDir 'Saved\Logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$name = [IO.Path]::GetFileNameWithoutExtension($scriptPath)
$logPath = Join-Path $logDir "py-$name-$stamp.log"
if ($LogPath) { $logPath = $LogPath }
$stdoutPath = Join-Path $logDir "py-$name-$stamp.stdout.txt"
$stderrPath = Join-Path $logDir "py-$name-$stamp.stderr.txt"

# Environment for the script.
$env:CP_PY_DIR = $pyDir
$env:CP_SCRIPT_ARGS = ($ScriptArgs -join ' ')
$env:CP_PROJECT_DIR = $projectDir
$env:CP_EDITOR_LOG = $logPath
$env:CP_RES_X = "$ResX"
$env:CP_RES_Y = "$ResY"
if ($Render) { $env:CP_RENDER = '1' } else { $env:CP_RENDER = '0' }

# Argument string. -ExecutePythonScript takes "<path> <args>"; quote the whole value when the path has spaces.
$pyArg = '-ExecutePythonScript="' + $scriptPath + '"'
$edArgs = @(
    ('"' + $uproject + '"'),
    $pyArg,
    '-unattended', '-nosplash', '-nopause', '-NoSound', '-nosourcecontrol', '-NoLoadingScreen',
    '-stdout', '-FullStdOutLogOutput',
    ('-abslog="' + $logPath + '"')
)
if ($Render) {
    # VERIFY: -RenderOffscreen on Windows. If no frames are produced, drop it and use -windowed (a window will flash up).
    $edArgs += @('-windowed', '-ForceRes', "-ResX=$ResX", "-ResY=$ResY", '-RenderOffscreen')
} else {
    $edArgs += '-nullrhi'
}
if ($null -ne $GraphicsAdapter) { $edArgs += "-graphicsadapter=$GraphicsAdapter" }
$edArgs += $ExtraEditorArgs

Write-Host "Running $([IO.Path]::GetFileName($scriptPath)) in $(if ($Render) { 'rendering' } else { 'nullrhi' }) mode"
Write-Host "  log: $logPath"
Write-Host "  DDC: ${env:UE-LocalDataCachePath}"
$p = Start-Process -FilePath $ue.EditorCmd -ArgumentList ($edArgs -join ' ') -PassThru -WindowStyle Hidden `
    -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
$null = $p.Handle
$sw = [Diagnostics.Stopwatch]::StartNew()
$timedOut = $false
if (-not $p.WaitForExit($TimeoutMinutes * 60 * 1000)) {
    $timedOut = $true
    & taskkill.exe /PID $p.Id /T /F | Out-Null
}
$sw.Stop()
$exit = if ($timedOut) { 124 } else { $p.ExitCode }

# Prefer the -abslog file; fall back to captured stdout if it was not written.
$lines = @()
if (Test-Path -LiteralPath $logPath) { $lines = @(Get-Content -LiteralPath $logPath) }
if ($lines.Count -eq 0 -and (Test-Path -LiteralPath $stdoutPath)) { $lines = @(Get-Content -LiteralPath $stdoutPath) }
$stderrLines = @()
if (Test-Path -LiteralPath $stderrPath) { $stderrLines = @(Get-Content -LiteralPath $stderrPath | Where-Object { $_ -and $_.Trim() }) }

# --- summary ---
$py = New-Object System.Collections.ArrayList
$errs = New-Object System.Collections.ArrayList
$crash = New-Object System.Collections.ArrayList
$modfail = New-Object System.Collections.ArrayList
$result = $null
$seen = @{}
foreach ($l in $lines) {
    if ($l -match 'CP_PY_RESULT:\s*(?<r>.*)$') { $result = $Matches['r'].Trim() }
    if ($l -match 'LogPython:') {
        [void]$py.Add(($l -replace '^\[[^\]]*\]\[[^\]]*\]', ''))
        if ($l -match 'LogPython:\s*(Error|Warning):' -or $l -match 'Traceback' -or $l -match '^\s*\S*LogPython: Error') {
            [void]$errs.Add(($l -replace '^\[[^\]]*\]\[[^\]]*\]', ''))
        }
        continue
    }
    if ($l -match 'Traceback \(most recent call last\)|^\s*File ".*", line \d+|^\s*\w*(Error|Exception): ') {
        [void]$errs.Add(($l -replace '^\[[^\]]*\]\[[^\]]*\]', ''))
        continue
    }
    if ($l -match 'Fatal error|Assertion failed|Unhandled Exception|Critical error|ensure condition failed|Access violation') {
        [void]$crash.Add($l.Trim())
    }
    elseif ($l -match '(LogPluginManager|LogModuleManager|LogProjectManager|LogInit|LogCompile|LogUObjectGlobals|LogLinker): (Error|Warning: Failed)|Failed to load|could not be loaded|Plugin .* failed to load|Missing modules|was not found') {
        $k = $l -replace '^\[[^\]]*\]\[[^\]]*\]', ''
        if (-not $seen.ContainsKey($k)) { $seen[$k] = 1; [void]$modfail.Add($k.Trim()) }
    }
}

Write-Host ''
Write-Host "PYTHON RUN SUMMARY: exit code $exit, $([math]::Round($sw.Elapsed.TotalSeconds)) s, $($lines.Count) log lines"
if ($timedOut) { Write-Host "TIMEOUT after $TimeoutMinutes min; process tree killed." }
if ($result) { Write-Host "Script result: $result" } else { Write-Host 'Script result: (no CP_PY_RESULT marker; the script did not finish, or Python did not run)' }
if ($crash.Count -gt 0) { Write-Host ''; Write-Host 'Crash / fatal markers:'; $crash | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" } }
if ($modfail.Count -gt 0) { Write-Host ''; Write-Host 'Module / plugin / load problems:'; $modfail | Select-Object -First 15 | ForEach-Object { Write-Host "  $_" } }
if ($errs.Count -gt 0) { Write-Host ''; Write-Host 'Python errors / warnings:'; $errs | Select-Object -First 40 | ForEach-Object { Write-Host "  $_" } }
Write-Host ''
Write-Host "LogPython lines (first $MaxPythonLines of $($py.Count)):"
$py | Select-Object -First $MaxPythonLines | ForEach-Object { Write-Host "  $_" }
if ($py.Count -gt $MaxPythonLines) { Write-Host "  ... $($py.Count - $MaxPythonLines) more in the log" }
if ($stderrLines.Count -gt 0) { Write-Host ''; Write-Host 'stderr (first 10):'; $stderrLines | Select-Object -First 10 | ForEach-Object { Write-Host "  $_" } }
Write-Host ''
Write-Host "Full log: $logPath"

if ($timedOut) { exit 124 }
if ($result -and $result -like 'OK*' -and $crash.Count -eq 0) { exit 0 }
exit 1
