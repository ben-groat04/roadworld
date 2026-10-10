<#
.SYNOPSIS
  Build CityPainterEditor (Win64 Development) from the command line and print a compact error summary.
.DESCRIPTION
  1. Finds the engine (Find-Unreal.ps1).
  2. Generates project files if CityPainter.sln is missing (non-fatal; the build does not need them). Skip with -SkipGenerate.
  3. Runs Engine\Build\BatchFiles\Build.bat CityPainterEditor Win64 Development -Project=... -WaitMutex -NoHotReload
     with all output going to Saved\Logs\build-<timestamp>.log (use -Stream to echo it live).
  4. Prints a deduplicated, file-grouped error summary (BuildLogParser.ps1), warning count, and the log path.
  Exit code 0 on success, 1 on build failure, 2 on setup problems (engine missing etc.).
  Windows PowerShell 5.1 compatible.
.EXAMPLE
  powershell -NoProfile -ExecutionPolicy Bypass -File unreal\tools\Build-Editor.ps1
#>
[CmdletBinding()]
param(
    [string]$EngineRoot,
    [string]$Target = 'CityPainterEditor',
    [string]$Configuration = 'Development',
    [int]$MaxErrors = 40,
    [int]$TimeoutMinutes = 90,
    [switch]$SkipGenerate,
    [switch]$Stream,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'BuildLogParser.ps1')

try {
    $ue = & (Join-Path $PSScriptRoot 'Find-Unreal.ps1') -EngineRoot $EngineRoot
} catch {
    Write-Host $_.Exception.Message
    exit 2
}

$projectDir = (Resolve-Path (Join-Path $PSScriptRoot '..\CityPainter')).Path
$uproject = Join-Path $projectDir 'CityPainter.uproject'
if (-not (Test-Path -LiteralPath $uproject)) { Write-Host "Missing $uproject"; exit 2 }

$logDir = Join-Path $projectDir 'Saved\Logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path $logDir "build-$stamp.log"

function Invoke-LoggedCmd {
    # Runs "<bat> <args>" through cmd.exe with stdout+stderr appended to $Log. Returns the exit code.
    param([string]$Bat, [string]$ArgString, [string]$Log, [int]$TimeoutMin, [switch]$Echo)
    # cmd /c "<command line>" : the whole command line is wrapped in one extra pair of quotes.
    $inner = '"' + $Bat + '" ' + $ArgString + ' >> "' + $Log + '" 2>&1'
    $p = Start-Process -FilePath 'cmd.exe' -ArgumentList ('/d /s /c "' + $inner + '"') -PassThru -WindowStyle Hidden
    $null = $p.Handle  # keep the handle so ExitCode is available after exit
    $pos = 0L
    $deadline = (Get-Date).AddMinutes($TimeoutMin)
    $lastBeat = Get-Date
    while (-not $p.HasExited) {
        Start-Sleep -Milliseconds 700
        if ($Echo -and (Test-Path -LiteralPath $Log)) {
            try {
                $fs = [IO.File]::Open($Log, 'Open', 'Read', 'ReadWrite')
                try {
                    if ($fs.Length -gt $pos) {
                        [void]$fs.Seek($pos, 'Begin')
                        $sr = New-Object IO.StreamReader($fs)
                        $chunk = $sr.ReadToEnd()
                        $pos = $fs.Position
                        if ($chunk) { Write-Host -NoNewline $chunk }
                    }
                } finally { $fs.Dispose() }
            } catch { }
        } elseif (-not $Echo -and ((Get-Date) - $lastBeat).TotalSeconds -ge 60) {
            Write-Host ("  ... still running ({0:N0} s)" -f ((Get-Date) - $p.StartTime).TotalSeconds)
            $lastBeat = Get-Date
        }
        if ((Get-Date) -gt $deadline) {
            & taskkill.exe /PID $p.Id /T /F | Out-Null
            Add-Content -LiteralPath $Log -Value "`r`nERROR: Build-Editor.ps1 timeout after $TimeoutMin minutes; process tree killed."
            return 124
        }
    }
    $p.WaitForExit()
    return $p.ExitCode
}

# --- 1. project files (optional) ---
$sln = Join-Path $projectDir 'CityPainter.sln'
if (-not $SkipGenerate -and -not (Test-Path -LiteralPath $sln)) {
    $gen = Join-Path $ue.Root 'Engine\Build\BatchFiles\GenerateProjectFiles.bat'
    if (Test-Path -LiteralPath $gen) {
        Write-Host 'Generating project files (first run)...'
        $genLog = Join-Path $logDir "genproject-$stamp.log"
        $gc = Invoke-LoggedCmd -Bat $gen -ArgString ('-project="' + $uproject + '" -game -engine') -Log $genLog -TimeoutMin 15
        if ($gc -ne 0) {
            Write-Host "GenerateProjectFiles exited $gc (non-fatal, the command-line build does not need a .sln). Log: $genLog"
            $gs = Get-BuildLogSummary -Lines (Get-Content -LiteralPath $genLog)
            if ($gs.ErrorCount -gt 0) { Write-Host (Format-BuildLogSummary -Summary $gs -Max 15 -LogPath $genLog) }
        }
    }
}

# --- 2. build ---
$uprojectQ = '"' + $uproject + '"'
$buildArgs = $Target + ' Win64 ' + $Configuration + ' -Project=' + $uprojectQ + ' -WaitMutex -NoHotReload'
if ($Clean) { $buildArgs += ' -Clean' }
Write-Host "Building $Target Win64 $Configuration ..."
Write-Host "  engine: $($ue.Root)"
Write-Host "  log:    $logPath"
Set-Content -LiteralPath $logPath -Value ("# Build-Editor.ps1 $stamp`r`n# " + $ue.BuildBat + ' ' + $buildArgs) -Encoding UTF8
$sw = [Diagnostics.Stopwatch]::StartNew()
$exit = Invoke-LoggedCmd -Bat $ue.BuildBat -ArgString $buildArgs -Log $logPath -TimeoutMin $TimeoutMinutes -Echo:$Stream
$sw.Stop()

# --- 3. summary ---
$lines = Get-Content -LiteralPath $logPath
$summary = Get-BuildLogSummary -Lines $lines
Write-Host ''
Write-Host (Format-BuildLogSummary -Summary $summary -Max $MaxErrors -LogPath $logPath)
Write-Host ''
$ok = ($exit -eq 0) -and ($summary.ErrorCount -eq 0)
if ($ok) {
    Write-Host ("BUILD SUCCEEDED in {0:N0} s (exit code 0)." -f $sw.Elapsed.TotalSeconds)
    exit 0
}
if ($exit -ne 0 -and $summary.ErrorCount -eq 0) {
    Write-Host "Build exited with code $exit but no recognised error lines; last 15 log lines:"
    $lines | Select-Object -Last 15 | ForEach-Object { Write-Host "  $_" }
}
Write-Host ("BUILD FAILED after {0:N0} s (exit code {1})." -f $sw.Elapsed.TotalSeconds, $exit)
exit 1
