$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Resolve-Path (Join-Path $scriptDir "..")
$outDir = Join-Path $root "test_output"

if (!(Test-Path $outDir)) {
    New-Item -Path $outDir -ItemType Directory | Out-Null
}

Set-Location $root

$exe = Join-Path $root "variant_harness.exe"
if (!(Test-Path $exe)) {
    $exe = Join-Path $root "variant_harness"
}
if (!(Test-Path $exe)) {
    throw "variant_harness executable not found in $root"
}

$commands = @(
    @{ Name = "basic_select"; Args = @("--select", "tests/filenames_basic.txt", "--profile", "default") },
    @{ Name = "basic_report"; Args = @("--report", "tests/filenames_basic.txt", "--profile", "default") },
    @{ Name = "memory_report"; Args = @("--report", "tests/filenames_memory.txt", "--profile", "default") },
    @{ Name = "language_report"; Args = @("--report", "tests/filenames_language.txt", "--profile", "default") },
    @{ Name = "special_report_only"; Args = @("--report", "tests/filenames_special_report_only.txt", "--profile", "default") },
    @{ Name = "grouping_false_positive_select"; Args = @("--select", "tests/grouping_false_positive_cases.txt", "--profile", "default") },
    @{ Name = "grouping_false_positive_report"; Args = @("--report", "tests/grouping_false_positive_cases.txt", "--profile", "default") },
    @{ Name = "singleton_exclude_select"; Args = @("--select", "tests/singleton_profile_exclude_cases.txt", "--profile", "tests/profiles/phase1_singleton_exclude.profile") },
    @{ Name = "singleton_exclude_report"; Args = @("--report", "tests/singleton_profile_exclude_cases.txt", "--profile", "tests/profiles/phase1_singleton_exclude.profile") },
    @{ Name = "bad_grouping_parse"; Args = @("--parse", "Game_v1.0.lha") },
    @{ Name = "bad_grouping_parse_demo"; Args = @("--parse", "GameDemo_v1.0.lha") },
    @{ Name = "bad_grouping_parse_2"; Args = @("--parse", "Game2_v1.0.lha") },
    @{ Name = "bad_grouping_parse_datadisk"; Args = @("--parse", "GameDataDisk_v1.0.lha") },
    @{ Name = "bad_grouping_parse_editor"; Args = @("--parse", "GameEditor_v1.0.lha") }
)

foreach ($item in $commands) {
    $file = Join-Path $outDir ($item.Name + ".txt")
    "### $exe $($item.Args -join ' ')" | Set-Content -Path $file
    try {
        $output = & $exe @($item.Args) 2>&1
        $exitCode = $LASTEXITCODE
        if ($output) {
            $output | Add-Content -Path $file
        }
        "exit_code: $exitCode" | Add-Content -Path $file
        if ($exitCode -ne 0) {
            throw "Command failed with exit code $exitCode"
        }
    }
    catch {
        if ($_.Exception.Message) {
            $_.Exception.Message | Add-Content -Path $file
        }
        "exit_code: nonzero" | Add-Content -Path $file
        throw
    }
}

# Memory resolve sweep to confirm aliases/mappings
$memFile = Join-Path $outDir "memory_resolve.txt"
$memTokens = @("512k", "512KB", "1MB", "1Mb", "1MBChip", "1MbChip", "1.5MB", "2MB", "8MB", "12MB", "15MB", "LowMem", "SlowMem", "Slow", "Fast", "Chip")
"### Memory resolve sweep" | Set-Content -Path $memFile
foreach ($token in $memTokens) {
    "--resolve Memory $token" | Add-Content -Path $memFile
    $output = & $exe --resolve Memory $token 2>&1
    $output | Add-Content -Path $memFile
    "" | Add-Content -Path $memFile
}

Write-Host "Milestone 4 test outputs written to $outDir"

function Write-CapturedOutput {
    param(
        [string]$OutputFile,
        [string[]]$CommandArgs
    )

    $output = & $exe @($CommandArgs) 2>&1
    $exitCode = $LASTEXITCODE
    if ($exitCode -ne 0) {
        throw "Command failed ($exitCode): $exe $($CommandArgs -join ' ')"
    }

    if ($null -eq $output) {
        "" | Set-Content -Path $OutputFile -NoNewline
    }
    else {
        ($output -join "`r`n") | Set-Content -Path $OutputFile -NoNewline
    }
}

function Get-NormalizedContent {
    param([string]$Path)

    if (!(Test-Path $Path)) {
        return ""
    }

    $raw = Get-Content -Raw -Path $Path
    if ($null -eq $raw) {
        return ""
    }

    return ($raw -replace "`r`n", "`n").TrimEnd("`n")
}

function Assert-FileMatchesExpected {
    param(
        [string]$Generated,
        [string]$Expected
    )

    $generatedNorm = Get-NormalizedContent -Path $Generated
    $expectedNorm = Get-NormalizedContent -Path $Expected

    if ($generatedNorm -ne $expectedNorm) {
        throw "Baseline mismatch: generated '$Generated' does not match expected '$Expected'"
    }
}

function Assert-FileContains {
    param(
        [string]$Path,
        [string]$Needle
    )

    $content = Get-NormalizedContent -Path $Path
    if ($content -notlike "*$Needle*") {
        throw "Expected '$Path' to contain '$Needle'"
    }
}

function Assert-CandidateCountAtLeast {
    param(
        [string]$Path,
        [int]$Minimum
    )

    $content = Get-NormalizedContent -Path $Path
    $match = [regex]::Match($content, "candidate_count:\s*(\d+)")
    if (!$match.Success) {
        throw "Could not find candidate_count in '$Path'"
    }

    $count = [int]$match.Groups[1].Value
    if ($count -lt $Minimum) {
        throw "Expected candidate_count >= $Minimum in '$Path', got $count"
    }
}

# Phase 2: regression baselines
$phase1GroupingFalsePositiveSelect = Join-Path $outDir "phase1_grouping_false_positive.select"
$phase1SingletonExcludeReport = Join-Path $outDir "phase1_singleton_exclude.report"
$phase1MemorySlowResolve = Join-Path $outDir "phase1_memory_alias_slow.resolve"
$phase1MemorySlowMemResolve = Join-Path $outDir "phase1_memory_alias_slowmem.resolve"

$phase2GamesSelect = Join-Path $outDir "phase2_games_small_real.select"
$phase2DemosSelect = Join-Path $outDir "phase2_demos_small_real.select"
$phase2LanguageReport = Join-Path $outDir "phase2_language_cases.report"
$phase3MemoryReport = Join-Path $outDir "phase3_games_memory_report.txt"
$phase3StressReport = Join-Path $outDir "phase3_games_stress_report.txt"

Write-CapturedOutput -OutputFile $phase1GroupingFalsePositiveSelect -CommandArgs @("--select", "tests/grouping_false_positive_cases.txt", "--profile", "default")
Write-CapturedOutput -OutputFile $phase1SingletonExcludeReport -CommandArgs @("--report", "tests/singleton_profile_exclude_cases.txt", "--profile", "tests/profiles/phase1_singleton_exclude.profile")
Write-CapturedOutput -OutputFile $phase1MemorySlowResolve -CommandArgs @("--resolve", "Memory", "Slow")
Write-CapturedOutput -OutputFile $phase1MemorySlowMemResolve -CommandArgs @("--resolve", "Memory", "SlowMem")

Write-CapturedOutput -OutputFile $phase2GamesSelect -CommandArgs @("--select", "tests/dat_samples/games_small_real.txt", "--profile", "default")
Write-CapturedOutput -OutputFile $phase2DemosSelect -CommandArgs @("--select", "tests/dat_samples/demos_small_real.txt", "--profile", "default")
Write-CapturedOutput -OutputFile $phase2LanguageReport -CommandArgs @("--report", "tests/dat_samples/language_cases.txt", "--profile", "default")
Write-CapturedOutput -OutputFile $phase3MemoryReport -CommandArgs @("--report", "tests/dat_samples/games_small_real.txt", "--profile", "default")
Write-CapturedOutput -OutputFile $phase3StressReport -CommandArgs @("--report", "tests/dat_samples/games_stress_large.txt", "--profile", "default")

Assert-FileMatchesExpected -Generated $phase1GroupingFalsePositiveSelect -Expected (Join-Path $root "tests/expected/grouping_false_positive.select")
Assert-FileMatchesExpected -Generated $phase1SingletonExcludeReport -Expected (Join-Path $root "tests/expected/singleton_profile_exclude.report")
Assert-FileMatchesExpected -Generated $phase1MemorySlowResolve -Expected (Join-Path $root "tests/expected/memory_alias_slow.resolve")
Assert-FileMatchesExpected -Generated $phase1MemorySlowMemResolve -Expected (Join-Path $root "tests/expected/memory_alias_slowmem.resolve")

Assert-FileMatchesExpected -Generated $phase2GamesSelect -Expected (Join-Path $root "tests/expected/games_small_real.select")
Assert-FileMatchesExpected -Generated $phase2DemosSelect -Expected (Join-Path $root "tests/expected/demos_small_real.select")
Assert-FileMatchesExpected -Generated $phase2LanguageReport -Expected (Join-Path $root "tests/expected/language_cases.report")
Assert-FileContains -Path $phase3MemoryReport -Needle "Memory estimate:"
Assert-FileContains -Path $phase3MemoryReport -Needle "candidate_count:"
Assert-FileContains -Path $phase3MemoryReport -Needle "peak_memory_estimate_bytes:"
Assert-FileContains -Path $phase3MemoryReport -Needle "candidate_lite_struct_size_bytes:"
Assert-FileContains -Path $phase3MemoryReport -Needle "candidate_lite_array_bytes:"
Assert-FileContains -Path $phase3MemoryReport -Needle "order_array_bytes:"
Assert-FileContains -Path $phase3MemoryReport -Needle "estimated_selector_peak_bytes:"
Assert-CandidateCountAtLeast -Path $phase3StressReport -Minimum 120

Write-Host "Phase 1 baseline comparisons passed"
Write-Host "Phase 2 baseline comparisons passed"
Write-Host "Phase 3 memory estimate output present"
Write-Host "Phase 3 stress candidate-count assertion passed"
