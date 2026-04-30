$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$root = Resolve-Path (Join-Path $scriptDir "..")
$outDir = Join-Path $root "test_output"
$outFile = Join-Path $outDir "resolve_smoke.txt"

if (!(Test-Path $outDir)) {
    New-Item -ItemType Directory -Path $outDir | Out-Null
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
    @{ Name = "Memory 1MbChip"; Args = @("--resolve", "Memory", "1MbChip") },
    @{ Name = "Memory slow"; Args = @("--resolve", "Memory", "slow") },
    @{ Name = "Language Fr"; Args = @("--resolve", "Language", "Fr") },
    @{ Name = "Chipset AGA"; Args = @("--resolve", "Chipset", "AGA") },
    @{ Name = "Video pal"; Args = @("--resolve", "Video", "pal") },
    @{ Name = "Media Disk"; Args = @("--resolve", "Media", "Disk") },
    @{ Name = "Memory NotARealToken"; Args = @("--resolve", "Memory", "NotARealToken") }
)

"Pass B resolve smoke run" | Set-Content -Path $outFile
"Generated: $(Get-Date -Format s)" | Add-Content -Path $outFile
"" | Add-Content -Path $outFile

foreach ($cmd in $commands) {
    "### $exe $($cmd.Args -join ' ')" | Add-Content -Path $outFile
    try {
        $output = & $exe @($cmd.Args) 2>&1
        $exitCode = $LASTEXITCODE
        if ($output) {
            $output | Add-Content -Path $outFile
        }
        "exit_code: $exitCode" | Add-Content -Path $outFile
    }
    catch {
        if ($_.Exception.Message) {
            $_.Exception.Message | Add-Content -Path $outFile
        }
        "exit_code: nonzero" | Add-Content -Path $outFile
    }
    "" | Add-Content -Path $outFile
}

Write-Host "Wrote $outFile"
