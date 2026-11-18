<#
    Test measurement, board deadlines, key events and OLED pages with a host C compiler.
    The generated test executable is written to the system temporary directory.
#>
[CmdletBinding()]
param([string]$Compiler = 'gcc')

$ErrorActionPreference = 'Stop'
$sourcePath = Join-Path (Split-Path -Parent $PSScriptRoot) 'System'
$testProgram = Join-Path ([IO.Path]::GetTempPath()) ("rlc-v1-7-{0}.exe" -f [guid]::NewGuid())
$algorithmSources = @(
    (Join-Path $PSScriptRoot 'test_rlc_measurement.c')
    (Join-Path $sourcePath 'RLC_Measure.c')
    (Join-Path $sourcePath 'RLC_Calculate.c')
    (Join-Path $sourcePath 'RLC_Calibration.c')
)
$boardSources = @(
    (Join-Path $PSScriptRoot 'test_board_deadline.c')
    (Join-Path $sourcePath 'BSP_Board.c')
    (Join-Path $sourcePath 'Range_Switch.c')
)
$keySources = @(
    (Join-Path $PSScriptRoot 'test_key.c')
    (Join-Path $sourcePath 'Key.c')
)
$oledSources = @(
    (Join-Path $PSScriptRoot 'test_oled_display.c')
    (Join-Path $sourcePath 'OLED_Display.c')
    (Join-Path $sourcePath 'OLED.c')
    (Join-Path $sourcePath 'OLED_Data.c')
    (Join-Path $sourcePath 'Text_Format.c')
)

function Invoke-HostTest([string]$Name, [string[]]$Sources) {
    & $Compiler '-std=c99' '-O2' '-Wall' '-Wextra' '-Werror' '-pedantic' '-DRLC_HOST_RELAY_TEST' "-I$sourcePath" @sources '-o' $testProgram '-lm'
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
    Invoke-HostTest -Name 'key events' -Sources $keySources
    Invoke-HostTest -Name 'OLED pages' -Sources $oledSources
    Invoke-HostTest -Name 'Full operator flow' -Sources @((Join-Path $PSScriptRoot 'test_app_flow.c'), (Join-Path $sourcePath 'App_RLC.c'))
} finally {
    if (Test-Path -LiteralPath $testProgram -PathType Leaf) {
        Remove-Item -LiteralPath $testProgram
    }
}
