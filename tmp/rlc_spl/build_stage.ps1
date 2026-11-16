param([Parameter(Mandatory = $true)][string]$Stage)

$ErrorActionPreference = 'Stop'
$projectDir = 'C:\Users\1\Desktop\集训电赛\测量代码V1.1'
$projectPath = Join-Path $projectDir 'project.uvprojx'
$logPath = Join-Path $PSScriptRoot "$Stage-Full.log"
$build = Start-Process -FilePath 'D:\keli5\UV4\UV4.exe' -WorkingDirectory $projectDir `
    -WindowStyle Hidden -ArgumentList @('-r', ('"' + $projectPath + '"'), '-t', 'Full', '-o', ('"' + $logPath + '"')) `
    -Wait -PassThru
$log = Get-Content -LiteralPath $logPath -Raw
if ($build.ExitCode -ne 0 -or $log -notmatch '0 Error\(s\), 0 Warning\(s\)') {
    Write-Output $log
    throw "Keil Full build failed at $Stage (exit $($build.ExitCode))."
}
Get-Content -LiteralPath $logPath | Where-Object { $_ -match '^Rebuild|^Program Size:|Error\(s\)' }
