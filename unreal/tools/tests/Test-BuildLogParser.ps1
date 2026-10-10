# Unit test for BuildLogParser.ps1 against tests\sample-build.log.
# Run: powershell -NoProfile -ExecutionPolicy Bypass -File Test-BuildLogParser.ps1
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '..\BuildLogParser.ps1')
$s = Get-BuildLogSummary -Lines (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'sample-build.log'))
$script:fail = 0
function Check($name, $cond) {
    if ($cond) { Write-Host "PASS $name" } else { Write-Host "FAIL $name"; $script:fail++ }
}
function Has($code, $file, $line) {
    @($s.Errors | Where-Object { ($_.Code -eq $code) -and ($_.File -like "*$file*") -and ($_.Line -eq $line) }).Count -ge 1
}
Check 'C# rules error CS0117 (Build.cs:22)' (Has 'CS0117' 'CityPainterSim.Build.cs' 22)
Check 'C# rules error CS0117 (Build.cs:23)' (Has 'CS0117' 'CityPainterSim.Build.cs' 23)
Check 'C# Target.cs CS0103' (Has 'CS0103' 'CityPainterEditor.Target.cs' 9)
Check 'C# kind' (@($s.Errors | Where-Object { $_.Kind -eq 'C#' }).Count -eq 3)
Check 'UBT bare ERROR line' (@($s.Errors | Where-Object { $_.Kind -eq 'UBT' -and $_.Message -like 'Expecting to find a type*' }).Count -eq 1)
Check 'UHT file(line) Error' (@($s.Errors | Where-Object { $_.Kind -eq 'UHT' -and $_.Line -eq 47 -and $_.File -like '*CPRoadNetworkActor.h' }).Count -eq 1)
Check 'UHT second' (@($s.Errors | Where-Object { $_.Kind -eq 'UHT' -and $_.Line -eq 52 }).Count -eq 1)
Check 'UHT bare "Error: Failed to generate"' (@($s.Errors | Where-Object { $_.Kind -eq 'UBT' -and $_.Message -like 'Failed to generate*' }).Count -eq 1)
Check 'MSVC fatal C1083 line only' (Has 'C1083' 'region_pack.h' 9)
Check 'MSVC C2065 deduped' ((@($s.Errors | Where-Object { $_.Code -eq 'C2065' }).Count -eq 1) -and (($s.Errors | Where-Object { $_.Code -eq 'C2065' }).Count -ne 0))
Check 'MSVC C2065 repeat count 2' ((($s.Errors | Where-Object { $_.Code -eq 'C2065' } | Select-Object -First 1).Count) -eq 2)
Check 'MSVC C2672' (Has 'C2672' 'CPRoadNetworkActor.cpp' 120)
Check 'MSVC path with spaces (C4577)' (Has 'C4577' 'filesystem' 30)
Check 'clang style file:line:col' (@($s.Errors | Where-Object { $_.File -like '*markings.cpp' -and $_.Line -eq 412 -and $_.Message -like 'use of undeclared*' }).Count -eq 1)
Check 'LNK2019 two distinct' (@($s.Errors | Where-Object { $_.Code -eq 'LNK2019' }).Count -eq 2)
Check 'LNK2019 file is the obj' (@($s.Errors | Where-Object { $_.Code -eq 'LNK2019' -and $_.File -eq 'Wrap_region_pack.cpp.obj' }).Count -eq 1)
Check 'LNK1120 with dll path (drive colon)' (@($s.Errors | Where-Object { $_.Code -eq 'LNK1120' -and $_.File -like '*UnrealEditor-CityPainterSim.dll' }).Count -eq 1)
Check 'LINK : LNK1181' (@($s.Errors | Where-Object { $_.Code -eq 'LNK1181' }).Count -eq 1)
Check 'cl : D8021' (@($s.Errors | Where-Object { $_.Code -eq 'D8021' }).Count -eq 1)
Check '"0 error(s)" summary line is not an error' (@($s.Errors | Where-Object { $_.Message -like '*0 warning*' }).Count -eq 0)
Check 'warnings counted (CS0618, C4996, C4458 = 3)' ($s.WarningCount -eq 3)
Check 'UBT Result captured' ($s.Result -eq 'Failed (OtherCompilationError)')
Check 'total distinct errors = 17' ($s.ErrorCount -eq 17)

$text = Format-BuildLogSummary -Summary $s -Max 5 -LogPath 'X:\log.txt'
Check 'cap honoured + remainder note' (($text -match 'more distinct error') -and ($text -match 'Full log: X:\\log.txt'))
Write-Host ''
Write-Host (Format-BuildLogSummary -Summary $s -Max 40 -LogPath 'X:\log.txt')
if ($script:fail -gt 0) { Write-Host "$($script:fail) FAILED"; exit 1 } else { Write-Host 'ALL PASSED' }
