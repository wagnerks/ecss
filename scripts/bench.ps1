#Requires -Version 5.1
<#
.SYNOPSIS
    Measures what ecss costs to compile against and how fast it runs.

.DESCRIPTION
    Two independent measurements, recorded under a label so runs can be compared:

      compile  three probe TUs under scripts/probes, each standing for one way of
               using the library (id type only / registry without iteration / views).
               Reports preprocessed line count, which is exact and noise-free, and
               wall-clock cl.exe time, which is what a build actually pays.

      runtime  the PerfBench cases from the gtest suite, so a header split can be
               shown not to have cost anything at run time.

    Results are JSON under build-bench/results, which .gitignore already covers.

.EXAMPLE
    ./scripts/bench.ps1 before
    ./scripts/bench.ps1 after
    ./scripts/bench.ps1 -Compare before,after

.EXAMPLE
    # Compile numbers only, which take seconds rather than minutes.
    ./scripts/bench.ps1 after -NoRuntime
#>
[CmdletBinding(DefaultParameterSetName = 'Record')]
param(
    [Parameter(ParameterSetName = 'Record', Position = 0, Mandatory = $true)]
    [string] $Label,

    [Parameter(ParameterSetName = 'Record')]
    [int] $Repeats = 3,

    [Parameter(ParameterSetName = 'Record')]
    [switch] $NoRuntime,

    # One pass is not enough to call a regression: the same binary has been seen to swing
    # 2.79-3.55 ms on Fair_Ungrouped_Each. Best-of-N narrows that to something comparable.
    [Parameter(ParameterSetName = 'Record')]
    [int] $RuntimeRuns = 3,

    [Parameter(ParameterSetName = 'Compare', Mandatory = $true)]
    [string[]] $Compare,

    # How compile time grows as a project declares more component types.
    [Parameter(ParameterSetName = 'Scaling', Mandatory = $true)]
    [switch] $Scaling,

    # String rather than int[]: powershell -File collapses "24,48" into one token, and as
    # an int[] that silently becomes 2448 rather than failing.
    [Parameter(ParameterSetName = 'Scaling')]
    [string[]] $Counts = @('1', '2', '4', '8', '16', '32'),

    [Parameter(ParameterSetName = 'Scaling')]
    [int] $ScalingRepeats = 2,

    # How many of the declared types the probe actually touches; 0 means all of them.
    # Pinning this while -Counts grows isolates the cost of merely declaring a type from
    # the cost of using one.
    [Parameter(ParameterSetName = 'Scaling')]
    [int] $UsedTypes = 0,

    # Spread the probe's work over one function per type instead of concentrating it in a
    # single large one, to tell front-end cost apart from optimiser cost.
    [Parameter(ParameterSetName = 'Scaling')]
    [switch] $SplitFunctions,

    # Extra compiler flags, for A/B-ing a change that sits behind a define. Record two
    # labels back to back, one with the flag and one without: the machine drifts enough
    # between sessions that comparing against yesterday's numbers proves nothing.
    [Parameter(ParameterSetName = 'Scaling')]
    [Parameter(ParameterSetName = 'Record')]
    [string[]] $ExtraFlags = @(),

    [Parameter(ParameterSetName = 'Scaling')]
    [ValidateSet('registry', 'view', 'each', 'all')]
    [string] $Flavors = 'all'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$EcssRoot  = Split-Path -Parent $PSScriptRoot
$ProbeDir  = Join-Path $PSScriptRoot 'probes'
$BenchDir  = Join-Path $EcssRoot 'build-bench'
$ResultDir = Join-Path $BenchDir 'results'
$ObjDir    = Join-Path $BenchDir 'obj'
$TestDir   = Join-Path $BenchDir 'tests'

# MSVC needs C++23 for the requires-clauses and consteval in the headers; on 19.3x
# that is still spelled latest.
$StdFlag = '/std:c++latest'

$script:ToolOutput = ''

# cl.exe and cmake both write progress to stderr, which the Stop preference would
# turn into a terminating error before the exit code is ever looked at.
function Invoke-Tool {
    param(
        [Parameter(Mandatory = $true)] [string]   $Exe,
        [Parameter(Mandatory = $true)] [string[]] $ToolArgs
    )
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $script:ToolOutput = (& $Exe @ToolArgs 2>&1 | Out-String)
        return $LASTEXITCODE
    }
    finally { $ErrorActionPreference = $prev }
}

function Assert-Tool {
    param([int] $Code, [string] $What)
    if ($Code -ne 0) {
        Write-Host ''
        Write-Host $script:ToolOutput -ForegroundColor Red
        throw "$What failed (exit $Code)"
    }
}

function Import-VsDevEnv {
    if ((Test-Path env:VSCMD_ARG_TGT_ARCH) -and $env:VSCMD_ARG_TGT_ARCH -eq 'x64') { return }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { throw 'vswhere.exe not found; is Visual Studio installed?' }

    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $vsPath) { throw 'No Visual Studio install with the C++ toolset.' }

    $vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
    if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found at $vcvars" }

    # Import the batch file's environment once, so each probe below is a bare cl.exe
    # call rather than another shell spawn inside the timed region.
    cmd /c "`"$vcvars`" >nul 2>&1 && set" | ForEach-Object {
        if ($_ -match '^([^=]+)=(.*)$') {
            Set-Item -Path "env:$($Matches[1])" -Value $Matches[2]
        }
    }
}

function Get-LineCount {
    param([string] $Path)
    $n = 0
    foreach ($line in [System.IO.File]::ReadLines($Path)) { $n++ }
    return $n
}

function Measure-Compile {
    param([int] $Repeats)

    New-Item -ItemType Directory -Force -Path $ObjDir | Out-Null
    $include = "/I$EcssRoot"
    $result = [ordered]@{}

    foreach ($probe in Get-ChildItem -Path $ProbeDir -Filter '*.cpp' | Sort-Object Name) {
        $name = [System.IO.Path]::GetFileNameWithoutExtension($probe.Name)
        Write-Host ("  {0,-16}" -f $name) -NoNewline

        # Preprocess to a file (/P) with no #line directives (/EP): the line count is the
        # deterministic half of the measurement, and it fails loudly if a probe went stale.
        $ppPath = Join-Path $ObjDir "$name.i"
        $code = Invoke-Tool 'cl' @('/nologo', '/P', '/EP', $StdFlag, $include, "/Fi$ppPath", $probe.FullName)
        Assert-Tool $code "preprocessing $name"
        $lines = Get-LineCount $ppPath

        $obj = Join-Path $ObjDir "$name.obj"
        $clArgs = @('/nologo', '/c', '/EHsc', '/O2', $StdFlag, $include, "/Fo$obj", $probe.FullName)

        $best = [double]::MaxValue
        for ($i = 0; $i -lt $Repeats; $i++) {
            $elapsed = Measure-Command { $script:clCode = Invoke-Tool 'cl' $clArgs }
            Assert-Tool $script:clCode "compiling $name"
            # Min rather than mean: every source of noise here only ever adds time.
            $best = [Math]::Min($best, $elapsed.TotalMilliseconds)
        }

        $result[$name] = [ordered]@{ lines = $lines; ms = [Math]::Round($best, 1) }
        Write-Host ("{0,10:N0} lines {1,8:N0} ms" -f $lines, $best)
    }

    return $result
}

function Measure-Runtime {
    param([int] $Runs, [string[]] $Extra)

    New-Item -ItemType Directory -Force -Path $TestDir | Out-Null

    Write-Host '  configuring' -NoNewline
    $cmakeArgs = @('-S', $EcssRoot, '-B', $TestDir, '-G', 'Ninja',
                   '-DCMAKE_BUILD_TYPE=Release', '-DECSS_BUILD_TESTS=ON')
    # Always passed, even when empty, so a previous run's flags do not linger in the cache
    # and the two halves of an A/B differ in nothing but the flag under test. Setting the
    # variable replaces CMake's MSVC defaults rather than adding to them, so they are
    # repeated here -- without /EHsc every TU warns C4530 and unwinding goes away.
    $cmakeArgs += "-DCMAKE_CXX_FLAGS=/DWIN32 /D_WINDOWS /EHsc $($Extra -join ' ')".TrimEnd()
    $code = Invoke-Tool 'cmake' $cmakeArgs
    Assert-Tool $code 'cmake configure'

    Write-Host ', building' -NoNewline
    $code = Invoke-Tool 'cmake' @('--build', $TestDir, '--target', 'ecss_tests')
    Assert-Tool $code 'cmake build'

    $exe = Get-ChildItem -Path $TestDir -Filter 'ecss_tests.exe' -Recurse | Select-Object -First 1
    if (-not $exe) { throw 'ecss_tests.exe not found after build' }

    Write-Host ", running $Runs times"
    $best = @{}
    for ($run = 0; $run -lt $Runs; $run++) {
        $code = Invoke-Tool $exe.FullName @('--gtest_filter=PerfBench.*')
        Assert-Tool $code 'PerfBench run'

        foreach ($line in ($script:ToolOutput -split "`r?`n")) {
            if ($line -notmatch '^\[Perf\]\s*(.+?):\s*(.+?)\s*$') { continue }
            $case, $value = $Matches[1], $Matches[2]

            # MTChurn puts its elapsed time in the part before the colon, so the raw text
            # is a different key every run. Blanking the digits keys the runs together.
            $key = $case -replace '[0-9]+(\.[0-9]+)?', '#'

            # Several cases report more than one figure on a line; rank on the first and
            # keep the line whole rather than trying to combine them.
            $rank = Get-FirstNumber $value
            if ($null -eq $rank) { continue }

            if (-not $best.ContainsKey($key)) {
                $best[$key] = @{ rank = $rank; case = $case; value = $value; order = $best.Count }
            }
            elseif ($rank -lt $best[$key].rank) {
                $best[$key].rank  = $rank
                $best[$key].case  = $case
                $best[$key].value = $value
            }
        }
    }
    if ($best.Count -eq 0) { throw 'no [Perf] lines in test output' }

    $result = [ordered]@{}
    foreach ($key in ($best.Keys | Sort-Object { $best[$_].order })) {
        $result[$best[$key].case] = $best[$key].value
        Write-Host ("    {0,-44} {1}" -f $best[$key].case, $best[$key].value)
    }
    return $result
}

# Writes a translation unit declaring $Count component types and then using the first $Used
# of them ($Used = 0 meaning all). Two knobs rather than one: with them tied together, a
# doubling doubles both the declared types and the operations on them, and the resulting
# curve cannot say which of the two is responsible for its shape.
function New-ScalingProbe {
    param([int] $Count, [string] $Flavor, [string] $Path, [int] $Used = 0, [switch] $Split)

    $useCount = if ($Used -le 0) { $Count } else { [Math]::Min($Used, $Count) }

    $sb = [System.Text.StringBuilder]::new()
    [void]$sb.AppendLine('#include <ecss/Registry.h>')
    [void]$sb.AppendLine('')
    for ($i = 0; $i -lt $Count; $i++) {
        [void]$sb.AppendLine("struct C$i { float x, y, z; };")
    }
    [void]$sb.AppendLine('')

    if ($Flavor -eq 'registry') {
        # -Split puts each type's work in its own function. Same instantiations, same
        # inlined code, but spread over many small functions instead of one large one --
        # which separates what the front end pays from what the optimiser pays.
        if ($Split) {
            for ($i = 0; $i -lt $useCount; $i++) {
                [void]$sb.AppendLine("void scalingProbe$i(ecss::Registry<>& reg, ecss::EntityId e) {")
                [void]$sb.AppendLine("`treg.addComponent<C$i>(e, 1.f, 2.f, 3.f);")
                [void]$sb.AppendLine("`tif (reg.hasComponent<C$i>(e)) { reg.destroyComponent<C$i>(e); }")
                [void]$sb.AppendLine("`tif (auto p = reg.pinComponent<const C$i>(e)) { (void)p->x; }")
                [void]$sb.AppendLine('}')
            }
        }
        else {
            [void]$sb.AppendLine('void scalingProbe(ecss::Registry<>& reg, ecss::EntityId e) {')
            for ($i = 0; $i -lt $useCount; $i++) {
                [void]$sb.AppendLine("`treg.addComponent<C$i>(e, 1.f, 2.f, 3.f);")
                [void]$sb.AppendLine("`tif (reg.hasComponent<C$i>(e)) { reg.destroyComponent<C$i>(e); }")
                [void]$sb.AppendLine("`tif (auto p = reg.pinComponent<const C$i>(e)) { (void)p->x; }")
            }
            [void]$sb.AppendLine('}')
        }
    }
    else {
        # 'view' goes through ArraysView::Iterator (range-for), 'each' through the callback
        # path. They instantiate different machinery, so they scale differently.
        if ($Split) {
            for ($i = 0; $i -lt [Math]::Max($useCount - 1, 1); $i++) {
                $j = [Math]::Min($i + 1, $Count - 1)
                [void]$sb.AppendLine("float scalingProbe$i(ecss::Registry<>& reg) {")
                [void]$sb.AppendLine("`tfloat s = 0.f;")
                if ($Flavor -eq 'each') {
                    [void]$sb.AppendLine("`treg.view<C$i, C$j>().each([&](C$i& a, C$j& b) { s += a.x + b.y; });")
                }
                else {
                    [void]$sb.AppendLine("`tfor (auto [e, a, b] : reg.view<C$i, C$j>()) { if (a && b) { s += a->x + b->y; } }")
                }
                [void]$sb.AppendLine("`treturn s;")
                [void]$sb.AppendLine('}')
            }
            Set-Content -Path $Path -Value $sb.ToString() -Encoding UTF8
            return
        }

        [void]$sb.AppendLine('float scalingProbe(ecss::Registry<>& reg) {')
        [void]$sb.AppendLine("`tfloat s = 0.f;")
        if ($useCount -eq 1) {
            if ($Flavor -eq 'each') {
                [void]$sb.AppendLine("`treg.view<C0>().each([&](C0& a) { s += a.x; });")
            }
            else {
                [void]$sb.AppendLine("`tfor (auto [e, a] : reg.view<C0>()) { if (a) { s += a->x; } }")
            }
        }
        else {
            # Adjacent pairs rather than every combination: one fresh ArraysView per added
            # type, which is what a project does as it grows, not N-squared of them.
            for ($i = 0; $i -lt $useCount - 1; $i++) {
                $j = $i + 1
                if ($Flavor -eq 'each') {
                    [void]$sb.AppendLine("`treg.view<C$i, C$j>().each([&](C$i& a, C$j& b) { s += a.x + b.y; });")
                }
                else {
                    [void]$sb.AppendLine("`tfor (auto [e, a, b] : reg.view<C$i, C$j>()) { if (a && b) { s += a->x + b->y; } }")
                }
            }
        }
        [void]$sb.AppendLine("`treturn s;")
        [void]$sb.AppendLine('}')
    }

    Set-Content -Path $Path -Value $sb.ToString() -Encoding UTF8
}

function Measure-Scaling {
    param([int[]] $Counts, [int] $Repeats, [string[]] $Extra, [string] $Which, [int] $Used = 0, [switch] $Split)

    $dir = Join-Path $BenchDir 'scaling'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $include = "/I$EcssRoot"
    $wanted = if ($Which -eq 'all') { @('registry', 'view', 'each') } else { @($Which) }

    foreach ($flavor in $wanted) {
        Write-Host ''
        Write-Host "$flavor" -ForegroundColor Cyan
        Write-Host ('{0,8} {1,11} {2,9} {3,14}' -f 'types', 'lines', 'ms', 'ms/type')

        $prevMs = $null
        $prevCount = 0
        foreach ($count in $Counts) {
            $cpp = Join-Path $dir "$flavor$count.cpp"
            New-ScalingProbe -Count $count -Flavor $flavor -Path $cpp -Used $Used -Split:$Split

            $pp = Join-Path $dir "$flavor$count.i"
            $code = Invoke-Tool 'cl' (@('/nologo', '/P', '/EP', $StdFlag, $include) + $Extra + @("/Fi$pp", $cpp))
            Assert-Tool $code "preprocessing $flavor/$count"
            $lines = Get-LineCount $pp

            $obj = Join-Path $dir "$flavor$count.obj"
            $clArgs = @('/nologo', '/c', '/EHsc', '/O2', $StdFlag, $include) + $Extra + @("/Fo$obj", $cpp)
            $best = [double]::MaxValue
            for ($i = 0; $i -lt $Repeats; $i++) {
                $elapsed = Measure-Command { $script:clCode = Invoke-Tool 'cl' $clArgs }
                Assert-Tool $script:clCode "compiling $flavor/$count"
                $best = [Math]::Min($best, $elapsed.TotalMilliseconds)
            }

            # Marginal cost, which is the number that says whether growth is linear.
            $marginal = ''
            if ($null -ne $prevMs) {
                $marginal = '{0,10:N1}' -f (($best - $prevMs) / ($count - $prevCount))
            }
            Write-Host ('{0,8} {1,11:N0} {2,9:N0} {3,14}' -f $count, $lines, $best, $marginal)

            $prevMs = $best
            $prevCount = $count
        }
    }
}

function Get-FirstNumber {
    param([string] $Text)
    if ($Text -match '([0-9]+(?:\.[0-9]+)?)') { return [double]$Matches[1] }
    return $null
}

function Format-Delta {
    param($Before, $After)
    $b = Get-FirstNumber "$Before"
    $a = Get-FirstNumber "$After"
    if ($null -eq $b -or $null -eq $a -or $b -eq 0) { return '' }
    $pct = (($a - $b) / $b) * 100
    return ('{0}{1:N1}%' -f $(if ($pct -ge 0) { '+' } else { '' }), $pct)
}

function Show-Comparison {
    param([string[]] $Labels)

    $runs = @{}
    foreach ($label in $Labels) {
        $path = Join-Path $ResultDir "$label.json"
        if (-not (Test-Path $path)) { throw "No recorded run named '$label' (looked in $ResultDir)" }
        $runs[$label] = Get-Content $path -Raw | ConvertFrom-Json
    }
    $first = $runs[$Labels[0]]
    $last  = $runs[$Labels[-1]]

    Write-Host ''
    Write-Host ("compile: {0} -> {1}" -f $Labels[0], $Labels[-1]) -ForegroundColor Cyan
    Write-Host ('{0,-16} {1,11} {2,11} {3,8}  {4,8} {5,8} {6,8}' -f 'probe', 'lines', 'lines', 'delta', 'ms', 'ms', 'delta')
    foreach ($probe in $first.compile.PSObject.Properties.Name) {
        if (@($last.compile.PSObject.Properties.Name) -notcontains $probe) { continue }
        $b = $first.compile.$probe
        $a = $last.compile.$probe
        Write-Host ('{0,-16} {1,11:N0} {2,11:N0} {3,8}  {4,8:N0} {5,8:N0} {6,8}' -f `
            $probe, $b.lines, $a.lines, (Format-Delta $b.lines $a.lines), `
            $b.ms, $a.ms, (Format-Delta $b.ms $a.ms))
    }

    $haveRuntime = (@($first.PSObject.Properties.Name) -contains 'runtime') -and
                   (@($last.PSObject.Properties.Name)  -contains 'runtime')
    if ($haveRuntime) {
        Write-Host ''
        Write-Host ("runtime: {0} -> {1}" -f $Labels[0], $Labels[-1]) -ForegroundColor Cyan
        foreach ($case in $first.runtime.PSObject.Properties.Name) {
            if (@($last.runtime.PSObject.Properties.Name) -notcontains $case) { continue }
            Write-Host ('{0,-44} {1,14} {2,14} {3,8}' -f `
                $case, $first.runtime.$case, $last.runtime.$case, `
                (Format-Delta $first.runtime.$case $last.runtime.$case))
        }
    }
    Write-Host ''
}

if ($PSCmdlet.ParameterSetName -eq 'Scaling') {
    Import-VsDevEnv
    $parsedCounts = @($Counts -split ',' | Where-Object { $_ } | ForEach-Object { [int]$_ })
    $parsedFlags = @($ExtraFlags -split ',' | Where-Object { $_ })
    Measure-Scaling -Counts $parsedCounts -Repeats $ScalingRepeats -Extra $parsedFlags -Which $Flavors -Used $UsedTypes -Split:$SplitFunctions
    Write-Host ''
    return
}

if ($PSCmdlet.ParameterSetName -eq 'Compare') {
    # powershell -File hands the whole "a,b" through as one string, so split it back.
    Show-Comparison -Labels ($Compare -split ',' | Where-Object { $_ })
    return
}

Import-VsDevEnv
New-Item -ItemType Directory -Force -Path $ResultDir | Out-Null

Write-Host "compile probes ($Repeats runs each, best kept)" -ForegroundColor Cyan
$compile = Measure-Compile -Repeats $Repeats

$record = [ordered]@{
    label    = $Label
    recorded = (Get-Date).ToString('s')
    compile  = $compile
}

if (-not $NoRuntime) {
    Write-Host ''
    Write-Host 'runtime perf' -ForegroundColor Cyan
    $record.runtime = Measure-Runtime -Runs $RuntimeRuns -Extra @($ExtraFlags -split ',' | Where-Object { $_ })
}

$outPath = Join-Path $ResultDir "$Label.json"
$record | ConvertTo-Json -Depth 6 | Set-Content -Path $outPath -Encoding UTF8

Write-Host ''
Write-Host "saved $outPath" -ForegroundColor Green
Write-Host "compare with: ./scripts/bench.ps1 -Compare <other>,$Label"
