# Parser for UnrealBuildTool / MSVC / linker / UHT / C# rules build logs.
# Dot-source:  . .\BuildLogParser.ps1
#   $s = Get-BuildLogSummary -Lines (Get-Content build.log)
#   Format-BuildLogSummary -Summary $s -Max 40 -LogPath build.log
# Windows PowerShell 5.1 compatible.

function Get-BuildLogSummary {
    param([string[]]$Lines)

    # Optional leading "[12/45] " style progress prefix or whitespace.
    $lead = '^\s*(?:\[\d+/\d+\]\s*)?'

    # MSVC compiler, C# compiler (Build.cs / Target.cs), also used by the linker when it gives file(line):
    #   file(12,5): error C2065: msg     file(12): fatal error C1083: msg     X.Build.cs(22,3): error CS0117: msg
    $reMsvc = $lead + '(?<file>.+?)\((?<line>\d+)(?:,\s*\d+)?(?:-\d+)?\)\s*:\s*(?:fatal\s+)?error\s+(?<code>[A-Za-z]+\d+)\s*:\s*(?<msg>.*)$'
    # Linker / driver, no line number:  foo.obj : error LNK2019: ...   LINK : fatal error LNK1181: ...   cl : Command line error D8021 : ...
    $reNoLine = $lead + '(?<file>[^:]+?(?::[\\/][^:]*?)?)\s*:\s*(?:fatal\s+)?(?:command line\s+)?error\s+(?<code>[A-Za-z]+\d+)\s*:?\s*(?<msg>.*)$'
    # UHT: file(line): Error: msg
    $reUht = $lead + '(?<file>.+?)\((?<line>\d+)(?:,\s*\d+)?\)\s*:\s*Error:\s*(?<msg>.*)$'
    # clang / gcc style: file:line:col: error: msg
    $reClang = $lead + '(?<file>(?:[A-Za-z]:)?[^:]+?):(?<line>\d+):(?:(?<col>\d+):)?\s*(?:fatal\s+)?error:\s*(?<msg>.*)$'
    # UBT: "ERROR: ..." / "LogCompile: Error: ..." / "Error: ..." (no file)
    $reBare = $lead + '(?:Log\w+:\s*)?(?:ERROR|Error):\s*(?<msg>.*)$'
    # warnings (count only)
    $reWarn = '(?:\)\s*:|^\s*[^\s:][^:]*\s:|:\d+:(?:\d+:)?)\s*warning\s+[A-Za-z]*\d+\s*:|\)\s*:\s*Warning:|:\d+:(?:\d+:)?\s*warning:|^\s*(?:Log\w+:\s*)?WARNING:'

    $errors = New-Object System.Collections.ArrayList
    $seen = @{}
    $warnings = 0
    $result = ''

    foreach ($raw in $Lines) {
        if ($null -eq $raw) { continue }
        $line = $raw.TrimEnd()
        if ($line.Length -eq 0) { continue }

        if ($line -match '^\s*Result:\s*(?<r>.+)$') { $result = $Matches['r'].Trim() }
        if ($line -match '(?i)^\s*(?:Total execution time|Build\s+FAILED)') { }

        $file = ''; $ln = 0; $code = ''; $msg = ''; $kind = ''; $hit = $false

        if ($line -match $reUht) {
            $file = $Matches['file']; $ln = [int]$Matches['line']; $msg = $Matches['msg']; $kind = 'UHT'; $hit = $true
        }
        elseif ($line -match $reMsvc) {
            $file = $Matches['file']; $ln = [int]$Matches['line']; $code = $Matches['code']; $msg = $Matches['msg']; $hit = $true
        }
        elseif ($line -match $reClang) {
            $file = $Matches['file']; $ln = [int]$Matches['line']; $msg = $Matches['msg']; $hit = $true
        }
        elseif ($line -match $reNoLine) {
            $file = $Matches['file'].Trim(); $code = $Matches['code']; $msg = $Matches['msg']; $hit = $true
        }
        elseif ($line -match $reBare) {
            $msg = $Matches['msg']; $kind = 'UBT'; $hit = $true
        }

        if (-not $hit) {
            if ($line -match $reWarn) { $warnings++ }
            continue
        }

        # Kind
        if (-not $kind) {
            if ($code -match '^LNK') { $kind = 'linker' }
            elseif ($code -match '^CS') { $kind = 'C#' }
            elseif ($file -match '\.(Build|Target)\.cs$') { $kind = 'C#' }
            elseif ($code -match '^[CD]\d+') { $kind = 'compiler' }
            elseif ($code) { $kind = 'tool' }
            else { $kind = 'compiler' }
        }
        $file = $file.Trim().Trim('"')
        $msg = $msg.Trim()
        if ($msg.Length -gt 240) { $msg = $msg.Substring(0, 237) + '...' }

        # Ignore "Error: 0" style noise such as "0 error(s)" is not matched by the patterns (needs "error CODE" / "Error:"), but
        # skip harmless bare UBT lines that merely say there were no errors.
        if ($kind -eq 'UBT' -and ($msg -match '^\s*$' -or $msg -match '^0\b')) { continue }

        $key = ($file.ToLowerInvariant()) + '|' + $ln + '|' + $code + '|' + $msg
        if ($seen.ContainsKey($key)) { $seen[$key].Count++; continue }
        $obj = [pscustomobject]@{ File = $file; Line = $ln; Code = $code; Message = $msg; Kind = $kind; Count = 1 }
        $seen[$key] = $obj
        [void]$errors.Add($obj)
    }

    [pscustomobject]@{
        Errors        = @($errors)
        ErrorCount    = $errors.Count
        ErrorLineHits = ($errors | Measure-Object -Property Count -Sum).Sum
        WarningCount  = $warnings
        Result        = $result
    }
}

function Format-BuildLogSummary {
    param($Summary, [int]$Max = 40, [string]$LogPath = '')
    $out = New-Object System.Collections.ArrayList
    $hits = $Summary.ErrorLineHits
    if ($null -eq $hits) { $hits = 0 }
    [void]$out.Add("BUILD SUMMARY: $($Summary.ErrorCount) distinct error(s) ($hits error line(s) total), $($Summary.WarningCount) warning line(s)" + $(if ($Summary.Result) { ", UBT result: $($Summary.Result)" } else { '' }))
    $shown = 0
    $groups = $Summary.Errors | Group-Object -Property File
    foreach ($g in $groups) {
        if ($shown -ge $Max) { break }
        $name = $g.Name
        if (-not $name) { $name = '(no file)' }
        [void]$out.Add("")
        [void]$out.Add($name)
        foreach ($e in $g.Group) {
            if ($shown -ge $Max) { break }
            $loc = ''
            if ($e.Line -gt 0) { $loc = " line $($e.Line)" }
            $codeText = ''
            if ($e.Code) { $codeText = " $($e.Code)" }
            $rep = ''
            if ($e.Count -gt 1) { $rep = " (x$($e.Count))" }
            [void]$out.Add("  [$($e.Kind)]$loc$codeText$rep : $($e.Message)")
            $shown++
        }
    }
    if ($Summary.ErrorCount -gt $shown) {
        [void]$out.Add("")
        [void]$out.Add("... $($Summary.ErrorCount - $shown) more distinct error(s) not shown (cap $Max). See the full log.")
    }
    if ($LogPath) { [void]$out.Add(""); [void]$out.Add("Full log: $LogPath") }
    return ($out -join "`r`n")
}
