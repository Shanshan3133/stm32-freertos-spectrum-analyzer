[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$binary = Join-Path $root 'build\target-stm32f446\spectrum_f446.bin'
if (-not (Test-Path -LiteralPath $binary)) {
    throw 'Normal target binary is missing; run tools/build_target.ps1 first.'
}
$programmer = Get-ChildItem -LiteralPath 'C:\ST' -Recurse `
    -Filter STM32_Programmer_CLI.exe -ErrorAction Stop |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $programmer) { throw 'STM32CubeProgrammer CLI was not found below C:\ST.' }

# Under-reset connection can replace a deliberately faulting watchdog build.
& $programmer -c port=SWD mode=UR freq=1000 -w $binary 0x08000000 -v -rst
if ($LASTEXITCODE -ne 0) { throw 'Flash or read-back verification failed.' }
Write-Host 'Normal firmware programmed and verified.'
