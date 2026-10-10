<#
.SYNOPSIS
  Locate the Unreal Engine install (default 5.8) without a hard-coded path.
.DESCRIPTION
  Order: -EngineRoot parameter, UE_ROOT env var, Epic launcher manifest
  (LauncherInstalled.dat), registry (HKLM:\SOFTWARE\EpicGames\Unreal Engine\<ver> InstalledDirectory),
  then a last-resort scan of <drive>:\UE_<ver> and <drive>:\Program Files\Epic Games\UE_<ver>.
  Each candidate is verified (Build.bat, UnrealEditor-Cmd.exe, RunUAT.bat). The first candidate that
  passes wins; if none pass, throws a message listing every candidate and what was missing
  (the launcher lists an engine before its download finishes, so "incomplete" is a real case).
  Returns an object: Root, Source, BuildBat, EditorCmd, EditorExe, RunUAT, Version.
  Dot-source this file to use Find-UnrealEngine / Get-UnrealCandidatesFromLauncherDat directly.
.EXAMPLE
  $ue = & .\Find-Unreal.ps1 ; $ue.Root
#>
[CmdletBinding()]
param(
    [string]$EngineRoot,
    [string]$Version = '5.8',
    [string]$LauncherDat = 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat',
    [switch]$Quiet
)

$script:RequiredFiles = @(
    'Engine\Build\BatchFiles\Build.bat',
    'Engine\Binaries\Win64\UnrealEditor-Cmd.exe',
    'Engine\Build\BatchFiles\RunUAT.bat'
)

function Get-UnrealCandidatesFromLauncherDat {
    # Returns install-location strings for entries whose AppName is UE_<Version> (exact, or UE_<Version>.x).
    param([string]$Path, [string]$Version)
    $result = @()
    if (-not (Test-Path -LiteralPath $Path)) { return $result }
    try {
        $json = Get-Content -LiteralPath $Path -Raw -ErrorAction Stop | ConvertFrom-Json -ErrorAction Stop
    } catch {
        return $result
    }
    if ($null -eq $json -or $null -eq $json.InstallationList) { return $result }
    $pattern = '^UE_' + [regex]::Escape($Version) + '(\.\d+)?$'
    foreach ($entry in @($json.InstallationList)) {
        if ($null -eq $entry) { continue }
        if ([string]$entry.AppName -match $pattern -and $entry.InstallLocation) {
            $result += [string]$entry.InstallLocation
        }
    }
    return $result
}

function Normalize-EngineRoot {
    # Accept either the folder that contains Engine\ or the Engine folder itself.
    param([string]$Path)
    $p = $Path.Trim().Trim('"').TrimEnd('\', '/')
    if ((Split-Path -Leaf $p) -ieq 'Engine') { $p = Split-Path -Parent $p }
    return $p
}

function Test-UnrealRoot {
    # Returns list of missing required files (empty list = complete install).
    param([string]$Root)
    $missing = @()
    foreach ($rel in $script:RequiredFiles) {
        if (-not (Test-Path -LiteralPath (Join-Path $Root $rel))) { $missing += $rel }
    }
    return $missing
}

function Find-UnrealEngine {
    param(
        [string]$EngineRoot,
        [string]$Version = '5.8',
        [string]$LauncherDat = 'C:\ProgramData\Epic\UnrealEngineLauncher\LauncherInstalled.dat',
        [switch]$Quiet
    )
    $cands = New-Object System.Collections.ArrayList
    function Add-Cand($path, $source) {
        if ([string]::IsNullOrWhiteSpace($path)) { return }
        [void]$cands.Add([pscustomobject]@{ Root = (Normalize-EngineRoot $path); Source = $source })
    }

    if ($EngineRoot) { Add-Cand $EngineRoot '-EngineRoot parameter' }
    if ($env:UE_ROOT) { Add-Cand $env:UE_ROOT 'UE_ROOT environment variable' }
    foreach ($loc in (Get-UnrealCandidatesFromLauncherDat -Path $LauncherDat -Version $Version)) {
        Add-Cand $loc "LauncherInstalled.dat (UE_$Version)"
    }
    foreach ($hive in @('HKLM:\SOFTWARE\EpicGames\Unreal Engine', 'HKLM:\SOFTWARE\WOW6432Node\EpicGames\Unreal Engine', 'HKCU:\SOFTWARE\Epic Games\Unreal Engine\Builds')) {
        try {
            $key = Join-Path $hive $Version
            if (Test-Path -LiteralPath $key) {
                $dir = (Get-ItemProperty -LiteralPath $key -ErrorAction Stop).InstalledDirectory
                if ($dir) { Add-Cand $dir "registry $key" }
            }
        } catch { }
    }
    # Last resort: well-known folder names on every fixed drive.
    try {
        foreach ($d in (Get-PSDrive -PSProvider FileSystem | Where-Object { $_.Root -match '^[A-Za-z]:[\\/]$' })) {
            Add-Cand (Join-Path $d.Root "UE_$Version") "drive scan $($d.Root)UE_$Version"
            Add-Cand (Join-Path $d.Root "Program Files\Epic Games\UE_$Version") "drive scan $($d.Root)Program Files\Epic Games\UE_$Version"
        }
    } catch { }

    $report = New-Object System.Collections.ArrayList
    $seen = @{}
    foreach ($c in $cands) {
        $key = $c.Root.ToLowerInvariant()
        if ($seen.ContainsKey($key)) { continue }
        $seen[$key] = $true
        if (-not (Test-Path -LiteralPath $c.Root)) {
            [void]$report.Add("  $($c.Root) [$($c.Source)]: folder does not exist")
            continue
        }
        $missing = Test-UnrealRoot -Root $c.Root
        if ($missing.Count -gt 0) {
            [void]$report.Add("  $($c.Root) [$($c.Source)]: INCOMPLETE, missing: $($missing -join ', ')")
            continue
        }
        $obj = [pscustomobject]@{
            Root      = $c.Root
            Source    = $c.Source
            Version   = $Version
            BuildBat  = Join-Path $c.Root 'Engine\Build\BatchFiles\Build.bat'
            EditorCmd = Join-Path $c.Root 'Engine\Binaries\Win64\UnrealEditor-Cmd.exe'
            EditorExe = Join-Path $c.Root 'Engine\Binaries\Win64\UnrealEditor.exe'
            RunUAT    = Join-Path $c.Root 'Engine\Build\BatchFiles\RunUAT.bat'
        }
        if (-not $Quiet) { Write-Host "Unreal Engine $Version found: $($obj.Root) (via $($obj.Source))" }
        return $obj
    }
    $msg = "Unreal Engine $Version not found or not fully installed."
    if ($report.Count -gt 0) { $msg += "`nCandidates tried:`n" + ($report -join "`n") }
    else { $msg += " No candidates (no -EngineRoot, no UE_ROOT, nothing in LauncherInstalled.dat '$LauncherDat', no registry key)." }
    $msg += "`nIf the Epic launcher is still downloading, wait for it to finish. Otherwise pass -EngineRoot <folder containing Engine> or set UE_ROOT."
    throw $msg
}

# Run only when invoked as a script, not when dot-sourced.
if ($MyInvocation.InvocationName -ne '.') {
    Find-UnrealEngine -EngineRoot $EngineRoot -Version $Version -LauncherDat $LauncherDat -Quiet:$Quiet
}
