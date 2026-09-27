[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sdk = Join-Path $root 'build\vendor\STM32CubeF4'
$revision = '5723be54530654c81a1c1ef9da39b2ce1c40564c'

if (-not (Test-Path -LiteralPath (Join-Path $sdk '.git'))) {
    New-Item -ItemType Directory -Path $sdk -Force | Out-Null
    & git -C $sdk init
    if ($LASTEXITCODE -ne 0) { throw 'Could not initialize SDK checkout.' }
    & git -C $sdk remote add origin https://github.com/STMicroelectronics/STM32CubeF4.git
    if ($LASTEXITCODE -ne 0) { throw 'Could not configure SDK remote.' }
    & git -C $sdk fetch --depth 1 origin $revision
    if ($LASTEXITCODE -ne 0) { throw 'Could not fetch pinned STM32CubeF4 revision.' }
    & git -C $sdk checkout --detach FETCH_HEAD
    if ($LASTEXITCODE -ne 0) { throw 'Could not check out pinned SDK revision.' }
}

$actual = (& git -C $sdk rev-parse HEAD).Trim()
if ($actual -ne $revision) {
    throw "Unexpected STM32CubeF4 revision: $actual (expected $revision)."
}

# Git for Windows invokes git-submodule as a shell script. Its usr/bin tools
# must be available even from a PowerShell terminal.
$gitExe = (Get-Command git -ErrorAction Stop).Source
$gitRoot = Split-Path -Parent (Split-Path -Parent $gitExe)
$gitUsrBin = Join-Path $gitRoot 'usr\bin'
if (Test-Path -LiteralPath $gitUsrBin) {
    $env:PATH = "$gitUsrBin;$env:PATH"
}
& git -C $sdk submodule update --init --depth 1 -- `
    Drivers/CMSIS/Device/ST/STM32F4xx `
    Drivers/STM32F4xx_HAL_Driver `
    Middlewares/Third_Party/FreeRTOS
if ($LASTEXITCODE -ne 0) { throw 'Could not initialize required SDK submodules.' }
Write-Host "STM32CubeF4 SDK ready at $sdk ($revision)"
