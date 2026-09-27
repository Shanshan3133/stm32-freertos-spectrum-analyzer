[CmdletBinding()]
param([int]$WatchdogFaultAfterBlocks = 0)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cubeIde = 'C:\ST\STM32CubeIDE_2.2.0\STM32CubeIDE'
$plugins = Join-Path $cubeIde 'plugins'
$gcc = Get-ChildItem -LiteralPath $plugins -Recurse -Filter arm-none-eabi-gcc.exe |
    Select-Object -First 1 -ExpandProperty FullName
$cmake = Get-ChildItem -LiteralPath $plugins -Recurse -Filter cmake.exe |
    Select-Object -First 1 -ExpandProperty FullName
$ninja = Get-ChildItem -LiteralPath $plugins -Recurse -Filter ninja.exe |
    Select-Object -First 1 -ExpandProperty FullName
if (-not $gcc -or -not $cmake -or -not $ninja) {
    throw 'ARM GCC, CMake, or Ninja is missing from STM32CubeIDE.'
}
$env:ARM_GCC_BIN = Split-Path -Parent $gcc
$env:PATH = "$(Split-Path -Parent $ninja);$env:PATH"
$source = Join-Path $root 'target\stm32f446'
$buildName = if ($WatchdogFaultAfterBlocks -gt 0) {
    'target-stm32f446-watchdog-fault'
} else {
    'target-stm32f446'
}
$build = Join-Path $root "build\$buildName"
& $cmake -S $source -B $build -G Ninja `
    "-DCMAKE_TOOLCHAIN_FILE=$source\arm-gcc.cmake" `
    "-DCMAKE_MAKE_PROGRAM=$ninja" `
    "-DWATCHDOG_FAULT_AFTER_BLOCKS=$WatchdogFaultAfterBlocks"
if ($LASTEXITCODE -ne 0) { throw 'CMake configuration failed.' }
& $cmake --build $build --parallel
if ($LASTEXITCODE -ne 0) { throw 'Target build failed.' }
Write-Host "Built $build\spectrum_f446.bin"
