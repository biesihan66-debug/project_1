<#
.SYNOPSIS
Rebuild and validate the STM32H743VI firmware with Keil uVision.
.DESCRIPTION
The target name selects a mode in project.uvprojx. Logs stay in Objects_<target>
and validated HEX images are copied to Firmware. The application uses Bank 1.
Calibration stays in Bank 2. This script only compiles and links the project.
#>
[CmdletBinding()]
param(
    [ValidateSet('Full', 'Basic', 'Fixed1k', 'All')]
    [string]$Target = 'Full',
    [string]$Uv4Path
)

$ErrorActionPreference = 'Stop'

if (-not $Uv4Path) {
    $installedUv4 = Get-Command UV4.exe -ErrorAction SilentlyContinue
    if ($installedUv4) {
        $Uv4Path = $installedUv4.Source
    } else {
        $Uv4Path = @(
            'D:\keli5\UV4\UV4.exe',
            'C:\Keil_v5\UV4\UV4.exe',
            'C:\Keil\UV4\UV4.exe'
        ) | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } |
            Select-Object -First 1
    }
}
if (-not $Uv4Path -or -not (Test-Path -LiteralPath $Uv4Path -PathType Leaf)) {
    throw 'Keil UV4.exe was not found. Supply -Uv4Path with its installed location.'
}
$Uv4Path = (Resolve-Path -LiteralPath $Uv4Path).Path

function Confirm-HexFlashRange {
    param([Parameter(Mandatory = $true)][string]$HexPath)

    $flashAddress = 0x08000000
    $applicationEnd = 0x08100000
    $addressBase = 0L
    $hasData = $false
    foreach ($line in Get-Content -LiteralPath $HexPath) {
        if ($line -notmatch '^:([0-9a-f]{2})([0-9a-f]{4})([0-9a-f]{2})([0-9a-f]*)[0-9a-f]{2}$') {
            throw "Invalid Intel HEX record in $HexPath"
        }
        $count = [Convert]::ToInt32($Matches[1], 16)
        $offset = [Convert]::ToInt32($Matches[2], 16)
        $type = $Matches[3]
        $data = $Matches[4]
        if ($data.Length -ne (2 * $count)) {
            throw "Invalid Intel HEX record length in $HexPath"
        }
        switch ($type) {
            '00' {
                $firstByte = $addressBase + $offset
                if ($count -lt 1 -or $firstByte -lt $flashAddress -or
                    ($firstByte + $count) -gt $applicationEnd) {
                    throw "HEX data lies outside STM32H743 Flash Bank 1: $HexPath"
                }
                $hasData = $true
            }
            '02' { $addressBase = [Convert]::ToInt64($data, 16) -shl 4 }
            '04' { $addressBase = [Convert]::ToInt64($data, 16) -shl 16 }
        }
    }
    if (-not $hasData) {
        throw "HEX has no application data: $HexPath"
    }
}

$projectPath = Join-Path $PSScriptRoot 'project.uvprojx'
$firmwarePath = Join-Path $PSScriptRoot 'Firmware'
$targetList = if ($Target -eq 'All') { 'Full', 'Basic', 'Fixed1k' } else { $Target }

foreach ($buildTarget in $targetList) {
    $objectDirectory = Join-Path $PSScriptRoot "Objects_$buildTarget"
    New-Item -ItemType Directory -Path $objectDirectory -Force | Out-Null
    $buildLog = Join-Path $objectDirectory ('build-' + [guid]::NewGuid().ToString('N') + '.log')
    $quotedProject = '"' + $projectPath + '"'
    $quotedLog = '"' + $buildLog + '"'
    $process = Start-Process -FilePath $Uv4Path -WorkingDirectory $PSScriptRoot -WindowStyle Hidden `
        -ArgumentList @('-r', $quotedProject, '-t', $buildTarget, '-o', $quotedLog) -PassThru -Wait

    if (-not (Test-Path -LiteralPath $buildLog -PathType Leaf)) {
        throw "uVision did not write a build log for $buildTarget (exit $($process.ExitCode))."
    }
    $report = Get-Content -LiteralPath $buildLog -Raw
    Move-Item -LiteralPath $buildLog -Destination (Join-Path $objectDirectory 'keil_build.log') -Force
    Write-Output $report
    if ($process.ExitCode -ne 0 -or $report -notmatch '0 Error\(s\), 0 Warning\(s\)') {
        throw "Keil build for $buildTarget did not complete cleanly."
    }
    if ($report -notmatch 'Program Size: Code=(\d+) RO-data=(\d+) RW-data=(\d+) ZI-data=(\d+)') {
        throw "Missing memory usage summary for $buildTarget."
    }
    $flashBytes = [int]$Matches[1] + [int]$Matches[2] + [int]$Matches[3]
    $ramBytes = [int]$Matches[3] + [int]$Matches[4]
    if ($flashBytes -gt 1MB -or $ramBytes -gt (512KB + 128KB)) {
        throw "Build for $buildTarget exceeds application Flash or SRAM."
    }
    $builtHex = Join-Path $objectDirectory "RLC_V1_4_$buildTarget.hex"
    Confirm-HexFlashRange -HexPath $builtHex
    New-Item -ItemType Directory -Path $firmwarePath -Force | Out-Null
    $releasedHex = Join-Path $firmwarePath "RLC_V1.4_$buildTarget.hex"
    Copy-Item -LiteralPath $builtHex -Destination $releasedHex
    Write-Output "$releasedHex (Flash $flashBytes bytes, SRAM $ramBytes bytes)"
}
