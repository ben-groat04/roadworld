# Tests Find-Unreal.ps1 parsing/verification against a fake install in a temp dir.
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File Test-FindUnreal.ps1
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\Find-Unreal.ps1')
$tmp = Join-Path ([IO.Path]::GetTempPath()) ("cp-find-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $tmp | Out-Null
$script:fail = 0
function Check($name, $cond) {
    if ($cond) { Write-Host "PASS $name" } else { Write-Host "FAIL $name"; $script:fail++ }
}
try {
    $good = Join-Path $tmp 'UE_5.8'
    $partial = Join-Path $tmp 'UE_5.8_partial'
    foreach ($rel in $script:RequiredFiles) {
        $f = Join-Path $good $rel
        New-Item -ItemType Directory -Force -Path (Split-Path $f) | Out-Null
        Set-Content $f 'x'
    }
    $f = Join-Path $partial $script:RequiredFiles[0]
    New-Item -ItemType Directory -Force -Path (Split-Path $f) | Out-Null
    Set-Content $f 'x'

    $dat = Join-Path $tmp 'LauncherInstalled.dat'
    $goodJ = $good.Replace('\', '\\')
    $partJ = $partial.Replace('\', '\\')

    Set-Content $dat ('{"InstallationList":[{"InstallLocation":"C:\\x\\UE_5.7","AppName":"UE_5.7"},{"InstallLocation":"' + $partJ + '","AppName":"UE_5.8"},{"InstallLocation":"C:\\Games\\Foo","AppName":"Foo"}]}')
    $c = @(Get-UnrealCandidatesFromLauncherDat -Path $dat -Version '5.8')
    Check 'dat parse selects only UE_5.8' ($c.Count -eq 1 -and $c[0] -eq $partial)
    Check 'incomplete detected' ((Test-UnrealRoot $partial).Count -eq 2)

    $e = ''
    try { Find-UnrealEngine -LauncherDat $dat -Quiet -EngineRoot (Join-Path $tmp 'nope') } catch { $e = $_.Exception.Message }
    Check 'incomplete/absent -> error mentioning INCOMPLETE' ($e -match 'INCOMPLETE')

    Set-Content $dat ('{"InstallationList":[{"InstallLocation":"' + $partJ + '","AppName":"UE_5.8"},{"InstallLocation":"' + $goodJ + '\\Engine","AppName":"UE_5.8.1"}]}')
    $r = Find-UnrealEngine -LauncherDat $dat -Quiet
    Check 'skips incomplete, takes complete, strips trailing Engine' (($r.Root -eq $good) -and ($r.Source -match 'LauncherInstalled'))

    $r2 = Find-UnrealEngine -LauncherDat (Join-Path $tmp 'missing.dat') -EngineRoot $good -Quiet
    Check '-EngineRoot override' ($r2.Root -eq $good)

    Set-Content $dat '{ not json'
    $r3 = Find-UnrealEngine -LauncherDat $dat -EngineRoot $good -Quiet
    Check 'corrupt dat tolerated' ($r3.Root -eq $good)

    Set-Content $dat '{"InstallationList":[]}'
    $env:UE_ROOT = $good
    $r4 = Find-UnrealEngine -LauncherDat $dat -Quiet
    Check 'UE_ROOT env fallback' ($r4.Root -eq $good)
}
finally {
    Remove-Item Env:\UE_ROOT -ErrorAction SilentlyContinue
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
}
if ($script:fail -gt 0) { Write-Host "$($script:fail) FAILED"; exit 1 } else { Write-Host 'ALL PASSED' }
