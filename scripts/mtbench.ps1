#Requires -Version 5.1
<#
.SYNOPSIS
    Build and run the fixed-work multi-threaded benchmarks.

.DESCRIPTION
    One command, because the point of these numbers is comparing two builds and nobody
    compares what is tedious to produce. Configures on first use, reuses the build directory
    afterwards, and prints the table.

    Unlike the MTChurn case in the gtest suite, every thread here does a fixed number of
    operations rather than running for a fixed time, so two runs are comparable.

.EXAMPLE
    ./scripts/mtbench.ps1

.EXAMPLE
    # Keep a copy to diff against after a change.
    ./scripts/mtbench.ps1 | Tee-Object build-bench-mt/before.txt
#>
[CmdletBinding()]
param(
    [string] $BuildDir = 'build-bench-mt',
    [string] $Config = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not (Test-Path (Join-Path $root "$BuildDir/CMakeCache.txt"))) {
    cmake -S $root -B (Join-Path $root $BuildDir) -DECSS_BUILD_BENCHMARKS=ON -DECSS_BUILD_TESTS=OFF | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "configure failed ($LASTEXITCODE)" }
}

cmake --build (Join-Path $root $BuildDir) --config $Config --target ecss_mtbench | Out-Null
if ($LASTEXITCODE -ne 0) { throw "build failed ($LASTEXITCODE)" }

# Single-config generators drop the exe straight in the build dir, multi-config in a subdir.
$exe = Get-ChildItem -Path (Join-Path $root $BuildDir) -Filter 'ecss_mtbench*' -Recurse |
       Where-Object { $_.Extension -in @('', '.exe') } | Select-Object -First 1
if (-not $exe) { throw 'ecss_mtbench not found after build' }

& $exe.FullName
