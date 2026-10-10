# Tests First-Run.ps1 (state file, skip/resume, -DryRun, failure propagation) against a fake engine:
# a fake Build.bat and a compiled fake UnrealEditor-Cmd.exe. Needs the .NET Framework csc.exe (present on Windows).
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File Test-FirstRun.ps1
$ErrorActionPreference = 'Stop'
$tools = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$project = (Resolve-Path (Join-Path $tools '..\CityPainter')).Path
$firstRun = Join-Path $tools 'First-Run.ps1'
$savedExisted = Test-Path (Join-Path $project 'Saved')
$tmp = Join-Path ([IO.Path]::GetTempPath()) ('cp-fr-' + [guid]::NewGuid().ToString('N'))
$eng = Join-Path $tmp 'UE'
$state = Join-Path $tmp 'state.json'
$png = Join-Path $project 'Saved\Screenshots\look\zz_firstrun_test.png'
$script:fail = 0
function Check($name, $cond) { if ($cond) { Write-Host "PASS $name" } else { Write-Host "FAIL $name"; $script:fail++ } }
function Run-FR($extra) {
    $o = & powershell -NoProfile -ExecutionPolicy Bypass -File $firstRun -EngineRoot $eng -StateFile $state @extra
    return @{ code = $LASTEXITCODE; text = ($o -join "`n") }
}
function St($n) { $j = Get-Content $state -Raw | ConvertFrom-Json; return $j.$n.status }

try {
    New-Item -ItemType Directory -Force "$eng\Engine\Build\BatchFiles", "$eng\Engine\Binaries\Win64" | Out-Null
    Set-Content "$eng\Engine\Build\BatchFiles\RunUAT.bat" '@echo off'
    $sample = Join-Path $PSScriptRoot 'sample-build.log'
    Set-Content "$eng\Engine\Build\BatchFiles\Build.bat" "@echo off`r`nif exist `"%~dp0fail.flag`" (type `"$sample`" & exit /b 6)`r`necho ok`r`nexit /b 0"
    Set-Content "$eng\Engine\Build\BatchFiles\GenerateProjectFiles.bat" '@echo off'
    Set-Content "$eng\Engine\Build\BatchFiles\fail.flag" 'x'
    $src = Join-Path $tmp 'fake.cs'
    Set-Content $src @'
using System; using System.IO; using System.Linq;
class P { static int Main(string[] a) {
  var l = a.First(x => x.StartsWith("-abslog=")).Substring(8).Trim('"');
  File.WriteAllLines(l, new[]{"LogPython: CP_PY_RESULT: OK"});
  var p = Environment.GetEnvironmentVariable("FAKE_PNG");
  if (!string.IsNullOrEmpty(p)) { Directory.CreateDirectory(Path.GetDirectoryName(p)); File.WriteAllText(p, "png"); }
  return 0; } }
'@
    & "$env:WINDIR\Microsoft.NET\Framework64\v4.0.30319\csc.exe" -nologo "-out:$eng\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" $src | Out-Null

    # 1. DryRun: lists steps, writes nothing.
    $r = Run-FR @('-DryRun')
    Check 'dryrun exit 0' ($r.code -eq 0)
    Check 'dryrun lists all steps' (($r.text -match 'find-engine') -and ($r.text -match 'build') -and ($r.text -match 'screenshots') -and ($r.text -match 'import_textures|textures'))
    Check 'dryrun writes no state' (-not (Test-Path $state))

    # 2. Build fails: stops, non-zero, later steps not run, rerun hint printed.
    $r = Run-FR @()
    Check 'build failure -> exit 1' ($r.code -eq 1)
    Check 'state: find-engine OK, build FAIL' ((St 'find-engine') -eq 'OK' -and (St 'build') -eq 'FAIL')
    Check 'later steps not recorded' ($null -eq (Get-Content $state -Raw | ConvertFrom-Json).environment)
    Check 'error summary printed' ($r.text -match 'BUILD SUMMARY')
    Check 'rerun hint printed' ($r.text -match '-Only build')

    # 3. Fix build, resume: find-engine skipped, build runs, screenshots fail (fake writes no PNG).
    Remove-Item "$eng\Engine\Build\BatchFiles\fail.flag"
    $r = Run-FR @()
    Check 'resume: find-engine skipped' ($r.text -match '\[find-engine\] already OK')
    Check 'resume: build OK, environment OK, level OK' ((St 'build') -eq 'OK' -and (St 'environment') -eq 'OK' -and (St 'level') -eq 'OK')
    Check 'screenshots without PNG -> FAIL, exit 1' ($r.code -eq 1 -and (St 'screenshots') -eq 'FAIL')
    Check 'summary lists a log path' ($r.text -match 'py-report_environment-')

    # 4. Rerun: everything OK is skipped; screenshots run again and now succeed.
    $env:FAKE_PNG = $png
    $r = Run-FR @()
    Check 'rerun skips build/environment/level' (($r.text -match '\[build\] already OK') -and ($r.text -match '\[environment\] already OK') -and ($r.text -match '\[level\] already OK'))
    Check 'rerun succeeds, PNG listed' ($r.code -eq 0 -and $r.text -match 'zz_firstrun_test.png')
    Check 'all OK in state' ((St 'screenshots') -eq 'OK')

    # 5. Fully done: nothing runs.
    $r = Run-FR @()
    Check 'all done: exit 0, nothing re-run' ($r.code -eq 0 -and $r.text -notmatch '\] RUN')

    # 6. -Only forces one step and invalidates later ones.
    $r = Run-FR @('-Only', 'environment')
    Check '-Only environment ran just that step' (($r.text -match '\[environment\] RUN') -and ($r.text -notmatch '\[level\]'))
    Check '-Only invalidates later steps' ($null -eq (Get-Content $state -Raw | ConvertFrom-Json).level)

    # 7. -From level re-runs level and screenshots only.
    $r = Run-FR @('-From', 'level')
    Check '-From level runs level+screenshots' (($r.text -match '\[level\] RUN') -and ($r.text -match '\[screenshots\] RUN') -and ($r.text -match '\[build\] already OK'))

    # 8. -Force redoes everything.
    $r = Run-FR @('-Force')
    Check '-Force reruns build' ($r.text -match '\[build\] RUN')
}
finally {
    Remove-Item Env:\FAKE_PNG -ErrorAction SilentlyContinue
    if (Test-Path $png) { Remove-Item $png -Force }
    Remove-Item -Recurse -Force $tmp -ErrorAction SilentlyContinue
    if (-not $savedExisted) { Remove-Item -Recurse -Force (Join-Path $project 'Saved') -ErrorAction SilentlyContinue }
    else { Get-ChildItem (Join-Path $project 'Saved\Logs') -Include 'build-*.log', 'genproject-*.log', 'py-*' -Recurse -ErrorAction SilentlyContinue | Where-Object { $_.LastWriteTime -gt (Get-Date).AddMinutes(-10) } | Remove-Item -Force -ErrorAction SilentlyContinue }
}
if ($script:fail -gt 0) { Write-Host "$($script:fail) FAILED"; exit 1 } else { Write-Host 'ALL PASSED' }
