cmake_minimum_required(VERSION 3.20)
project(stm32_freertos_spectrum C ASM)

set(WATCHDOG_FAULT_AFTER_BLOCKS 0 CACHE STRING
    "Suspend DSP after N blocks for watchdog-reset validation (0 disables)")
option(FREEZE_ADC_FAULT "Freeze ADC samples for signal-fault validation" OFF)
option(USE_STLINK_MCO_CLOCK
    "Use the 8 MHz ST-LINK MCO via HSE bypass on MB1136 C-02+ boards" OFF)

set(STM32CUBE_F4_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../build/vendor/STM32CubeF4"
    CACHE PATH "STM32CubeF4 checkout with HAL, device and FreeRTOS submodules")
set(F4 "${STM32CUBE_F4_ROOT}")
set(HAL "${F4}/Drivers/STM32F4xx_HAL_Driver")
set(DEVICE "${F4}/Drivers/CMSIS/Device/ST/STM32F4xx")
set(FREERTOS "${F4}/Middlewares/Third_Party/FreeRTOS/Source")
set(DSP "${F4}/Drivers/CMSIS/DSP")
set(REPO "${CMAKE_CURRENT_SOURCE_DIR}/../..")

foreach(required
    "${HAL}/Inc/stm32f4xx_hal.h"
    "${DEVICE}/Include/stm32f4xx.h"
    "${FREERTOS}/include/FreeRTOS.h"
    "${DSP}/Include/arm_math.h")
  if(NOT EXISTS "${required}")
    message(FATAL_ERROR "Missing ${required}; initialize the documented ST submodules")
  endif()
endforeach()

set(HAL_SOURCES
    stm32f4xx_hal.c stm32f4xx_hal_adc.c stm32f4xx_hal_adc_ex.c
    stm32f4xx_hal_cortex.c stm32f4xx_hal_dac.c stm32f4xx_hal_dac_ex.c
    stm32f4xx_hal_dma.c stm32f4xx_hal_dma_ex.c stm32f4xx_hal_flash.c
    stm32f4xx_hal_flash_ex.c stm32f4xx_hal_gpio.c stm32f4xx_hal_iwdg.c
    stm32f4xx_hal_pwr.c stm32f4xx_hal_pwr_ex.c stm32f4xx_hal_rcc.c
    stm32f4xx_hal_rcc_ex.c stm32f4xx_hal_tim.c stm32f4xx_hal_tim_ex.c
    stm32f4xx_hal_uart.c)
list(TRANSFORM HAL_SOURCES PREPEND "${HAL}/Src/")

set(DSP_SOURCES
    BasicMathFunctions/arm_mult_q15.c
    BasicMathFunctions/arm_shift_q15.c
    CommonTables/arm_common_tables.c
    CommonTables/arm_const_structs.c
    ComplexMathFunctions/arm_cmplx_mag_q15.c
    FastMathFunctions/arm_cos_f32.c
    FastMathFunctions/arm_sqrt_q31.c
    TransformFunctions/arm_bitreversal2.c
    TransformFunctions/arm_cfft_init_q15.c
    TransformFunctions/arm_cfft_q15.c
    TransformFunctions/arm_cfft_radix4_q15.c
    TransformFunctions/arm_rfft_init_q15.c
    TransformFunctions/arm_rfft_q15.c)
list(TRANSFORM DSP_SOURCES PREPEND "${DSP}/Source/")

add_executable(spectrum_f446.elf
    "${DEVICE}/Source/Templates/gcc/startup_stm32f446xx.s"
    "${DEVICE}/Source/Templates/system_stm32f4xx.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/board_peripherals.c"
    "${REPO}/platform/stm32f446/board_skeleton.c"
    "${REPO}/platform/stm32f446/spectrum_cmsis.c"
    "${REPO}/app/main.c" "${REPO}/app/tasks.c"
    "${REPO}/core/health_monitor.c" "${REPO}/core/telemetry.c"
    "${FREERTOS}/tasks.c" "${FREERTOS}/queue.c" "${FREERTOS}/list.c"
    "${FREERTOS}/portable/GCC/ARM_CM4F/port.c"
    "${FREERTOS}/portable/MemMang/heap_4.c"
    ${HAL_SOURCES} ${DSP_SOURCES})

target_include_directories(spectrum_f446.elf PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${REPO}/include"
    "${HAL}/Inc"
    "${DEVICE}/Include"
    "${F4}/Drivers/CMSIS/Core/Include"
    "${FREERTOS}/include"
    "${FREERTOS}/portable/GCC/ARM_CM4F"
    "${DSP}/Include"
    "${DSP}/PrivateInclude")

target_compile_definitions(spectrum_f446.elf PRIVATE
    STM32F446xx USE_HAL_DRIVER ANALYZER_STM32F446
    ANALYZER_USE_CMSIS_DSP ARM_MATH_CM4
    FAULT_INJECT_DSP_STALL_AFTER_BLOCKS=${WATCHDOG_FAULT_AFTER_BLOCKS}u)
if(USE_STLINK_MCO_CLOCK)
  target_compile_definitions(spectrum_f446.elf PRIVATE USE_STLINK_MCO_CLOCK=1)
endif()
if(FREEZE_ADC_FAULT)
  target_compile_definitions(spectrum_f446.elf PRIVATE FAULT_INJECT_FREEZE_ADC=1)
endif()

target_compile_options(spectrum_f446.elf PRIVATE
    -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
    -O2 -ffunction-sections -fdata-sections -Wall -Wextra)

set(LINKER "${F4}/Projects/STM32446E-Nucleo/Templates/STM32CubeIDE/STM32F446RETX_FLASH.ld")
target_link_options(spectrum_f446.elf PRIVATE
    -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard
    -T${LINKER} -specs=nosys.specs -specs=nano.specs
    -Wl,--gc-sections -Wl,-Map=${CMAKE_CURRENT_BINARY_DIR}/spectrum_f446.map)
target_link_libraries(spectrum_f446.elf PRIVATE m)

add_custom_command(TARGET spectrum_f446.elf POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:spectrum_f446.elf>
            "${CMAKE_CURRENT_BINARY_DIR}/spectrum_f446.bin"
    COMMAND ${CMAKE_SIZE} $<TARGET_FILE:spectrum_f446.elf>)
