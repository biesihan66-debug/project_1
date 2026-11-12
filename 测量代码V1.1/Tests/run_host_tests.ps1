<#
    Test the measurement algorithm and board setting deadlines with a host C compiler.
    The generated test executable is written to the system temporary directory.
#>
[CmdletBinding()]
param([string]$Compiler = 'gcc')

$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path (Split-Path -Parent $PSScriptRoot) 'System'
$testProgram = Join-Path ([IO.Path]::GetTempPath()) ("rlc-v1-1-{0}.exe" -f [guid]::NewGuid())
$algorithmSources = @(
    (Join-Path $PSScriptRoot 'test_rlc_measurement.c')
    (Join-Path $sourcePath 'automatic_rlc_measurement.c')
    (Join-Path $sourcePath 'rlc_impedance_calculation.c')
    (Join-Path $sourcePath 'open_short_calibration.c')
)
$boardSources = @(
    (Join-Path $PSScriptRoot 'test_board_deadline.c')
    (Join-Path $sourcePath 'stm32_rlc_board.c')
)

function Invoke-HostTest([string]$Name, [string[]]$Sources) {
    & $Compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-pedantic' "-I$sourcePath" @sources '-o' $testProgram '-lm'
    if ($LASTEXITCODE -ne 0) {
        throw "Host compiler for $Name exited with $LASTEXITCODE."
    }
    & $testProgram
    if ($LASTEXITCODE -ne 0) {
        throw "RLC host test $Name exited with $LASTEXITCODE."
    }
}

try {
    Invoke-HostTest -Name 'measurement algorithm' -Sources $algorithmSources
    Invoke-HostTest -Name 'board setting deadlines' -Sources $boardSources
} finally {
    if (Test-Path -LiteralPath $testProgram -PathType Leaf) {
        Remove-Item -LiteralPath $testProgram
    }
}
