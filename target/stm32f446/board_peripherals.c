#include "stm32f4xx_hal.h"

#include "FreeRTOS.h"
#include "task.h"

extern void xPortSysTickHandler(void);

void vApplicationGetIdleTaskMemory(StaticTask_t **task,
                                   StackType_t **stack,
                                   uint32_t *stack_size) {
    static StaticTask_t idle_task;
    static StackType_t idle_stack[configMINIMAL_STACK_SIZE];
    *task = &idle_task;
    *stack = idle_stack;
    *stack_size = configMINIMAL_STACK_SIZE;
}

ADC_HandleTypeDef hadc1;
ADC_HandleTypeDef hadc2;
DAC_HandleTypeDef hdac;
TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim5;
UART_HandleTypeDef huart2;
IWDG_HandleTypeDef hiwdg;

static DMA_HandleTypeDef hdma_adc1;
static DMA_HandleTypeDef hdma_dac1;
static DMA_HandleTypeDef hdma_usart2_tx;

static void require_hal(HAL_StatusTypeDef status) {
    if (status != HAL_OK) {
        __disable_irq();
        for (;;) { }
    }
}

void SystemClock_Config(void) {
    __HAL_RCC_PWR_CLK_ENABLE();
    __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE1);

    RCC_OscInitTypeDef oscillator = {0};
#if defined(USE_STLINK_MCO_CLOCK)
    /* MB1136 C-02+ routes the ST-LINK 8 MHz MCO to OSC_IN by default. */
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    oscillator.HSEState = RCC_HSE_BYPASS;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    oscillator.PLL.PLLM = 8;
#else
    oscillator.OscillatorType = RCC_OSCILLATORTYPE_HSI;
    oscillator.HSIState = RCC_HSI_ON;
    oscillator.HSICalibrationValue = RCC_HSICALIBRATION_DEFAULT;
    oscillator.PLL.PLLSource = RCC_PLLSOURCE_HSI;
    oscillator.PLL.PLLM = 16;
#endif
    oscillator.PLL.PLLState = RCC_PLL_ON;
    oscillator.PLL.PLLN = 360;
    oscillator.PLL.PLLP = RCC_PLLP_DIV2;
    oscillator.PLL.PLLQ = 8;
    require_hal(HAL_RCC_OscConfig(&oscillator));
    require_hal(HAL_PWREx_EnableOverDrive());

    RCC_ClkInitTypeDef clocks = {0};
    clocks.ClockType = RCC_CLOCKTYPE_HCLK | RCC_CLOCKTYPE_SYSCLK |
                       RCC_CLOCKTYPE_PCLK1 | RCC_CLOCKTYPE_PCLK2;
    clocks.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    clocks.AHBCLKDivider = RCC_SYSCLK_DIV1;
    clocks.APB1CLKDivider = RCC_HCLK_DIV4;
    clocks.APB2CLKDivider = RCC_HCLK_DIV2;
    require_hal(HAL_RCC_ClockConfig(&clocks, FLASH_LATENCY_5));
}

void MX_GPIO_Init(void) {
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef pin = {0};
    pin.Pin = GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_4;
    pin.Mode = GPIO_MODE_ANALOG;
    pin.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOA, &pin);

    pin.Pin = GPIO_PIN_2 | GPIO_PIN_3;
    pin.Mode = GPIO_MODE_AF_PP;
    pin.Pull = GPIO_PULLUP;
    pin.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    pin.Alternate = GPIO_AF7_USART2;
    HAL_GPIO_Init(GPIOA, &pin);
}

void MX_DMA_Init(void) {
    __HAL_RCC_DMA1_CLK_ENABLE();
    __HAL_RCC_DMA2_CLK_ENABLE();
    HAL_NVIC_SetPriority(DMA2_Stream0_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA2_Stream0_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream5_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream5_IRQn);
    HAL_NVIC_SetPriority(DMA1_Stream6_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(DMA1_Stream6_IRQn);
    HAL_NVIC_SetPriority(ADC_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(ADC_IRQn);
}

static void init_adc(ADC_HandleTypeDef *hadc, ADC_TypeDef *instance,
                     uint32_t channel, uint32_t trigger_edge) {
    hadc->Instance = instance;
    hadc->Init.ClockPrescaler = ADC_CLOCK_SYNC_PCLK_DIV4;
    hadc->Init.Resolution = ADC_RESOLUTION_12B;
    hadc->Init.ScanConvMode = DISABLE;
    hadc->Init.ContinuousConvMode = DISABLE;
    hadc->Init.DiscontinuousConvMode = DISABLE;
    hadc->Init.ExternalTrigConvEdge = trigger_edge;
    hadc->Init.ExternalTrigConv = ADC_EXTERNALTRIGCONV_T2_TRGO;
    hadc->Init.DataAlign = ADC_DATAALIGN_RIGHT;
    hadc->Init.NbrOfConversion = 1;
    hadc->Init.DMAContinuousRequests = ENABLE;
    hadc->Init.EOCSelection = ADC_EOC_SINGLE_CONV;
    require_hal(HAL_ADC_Init(hadc));

    ADC_ChannelConfTypeDef config = {0};
    config.Channel = channel;
    config.Rank = 1;
    config.SamplingTime = ADC_SAMPLETIME_15CYCLES;
    require_hal(HAL_ADC_ConfigChannel(hadc, &config));
}

void MX_ADC1_Init(void) {
    __HAL_RCC_ADC1_CLK_ENABLE();
    __HAL_RCC_ADC2_CLK_ENABLE();
    init_adc(&hadc1, ADC1, ADC_CHANNEL_0,
             ADC_EXTERNALTRIGCONVEDGE_RISING);

    ADC_MultiModeTypeDef multimode = {0};
    multimode.Mode = ADC_DUALMODE_REGSIMULT;
    multimode.DMAAccessMode = ADC_DMAACCESSMODE_2;
    multimode.TwoSamplingDelay = ADC_TWOSAMPLINGDELAY_5CYCLES;
    require_hal(HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode));

    hdma_adc1.Instance = DMA2_Stream0;
    hdma_adc1.Init.Channel = DMA_CHANNEL_0;
    hdma_adc1.Init.Direction = DMA_PERIPH_TO_MEMORY;
    hdma_adc1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_adc1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_adc1.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_adc1.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_adc1.Init.Mode = DMA_CIRCULAR;
    hdma_adc1.Init.Priority = DMA_PRIORITY_VERY_HIGH;
    hdma_adc1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    require_hal(HAL_DMA_Init(&hdma_adc1));
    __HAL_LINKDMA(&hadc1, DMA_Handle, hdma_adc1);
}

void MX_ADC2_Init(void) {
    init_adc(&hadc2, ADC2, ADC_CHANNEL_1,
             ADC_EXTERNALTRIGCONVEDGE_NONE);
}

void MX_DAC_Init(void) {
    __HAL_RCC_DAC_CLK_ENABLE();
    hdac.Instance = DAC;
    require_hal(HAL_DAC_Init(&hdac));
    DAC_ChannelConfTypeDef config = {0};
    config.DAC_Trigger = DAC_TRIGGER_T2_TRGO;
    config.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
    require_hal(HAL_DAC_ConfigChannel(&hdac, &config, DAC_CHANNEL_1));

    hdma_dac1.Instance = DMA1_Stream5;
    hdma_dac1.Init.Channel = DMA_CHANNEL_7;
    hdma_dac1.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_dac1.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_dac1.Init.MemInc = DMA_MINC_ENABLE;
    hdma_dac1.Init.PeriphDataAlignment = DMA_PDATAALIGN_WORD;
    hdma_dac1.Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
    hdma_dac1.Init.Mode = DMA_CIRCULAR;
    hdma_dac1.Init.Priority = DMA_PRIORITY_HIGH;
    hdma_dac1.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    require_hal(HAL_DMA_Init(&hdma_dac1));
    __HAL_LINKDMA(&hdac, DMA_Handle1, hdma_dac1);
}

static void init_timer(TIM_HandleTypeDef *timer, TIM_TypeDef *instance,
                       uint32_t prescaler, uint32_t period,
                       uint32_t master_trigger) {
    timer->Instance = instance;
    timer->Init.Prescaler = prescaler;
    timer->Init.CounterMode = TIM_COUNTERMODE_UP;
    timer->Init.Period = period;
    timer->Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
    timer->Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
    require_hal(HAL_TIM_Base_Init(timer));
    TIM_MasterConfigTypeDef master = {0};
    master.MasterOutputTrigger = master_trigger;
    master.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
    require_hal(HAL_TIMEx_MasterConfigSynchronization(timer, &master));
}

void MX_TIM2_Init(void) {
    __HAL_RCC_TIM2_CLK_ENABLE();
    /* APB1=45 MHz; timer clock doubles to 90 MHz. */
    init_timer(&htim2, TIM2, 0, 899, TIM_TRGO_UPDATE);
}

void MX_TIM5_Init(void) {
    __HAL_RCC_TIM5_CLK_ENABLE();
    init_timer(&htim5, TIM5, 89, 0xFFFFFFFFu, TIM_TRGO_RESET);
}

void MX_USART2_UART_Init(void) {
    __HAL_RCC_USART2_CLK_ENABLE();
    huart2.Instance = USART2;
    huart2.Init.BaudRate = 921600;
    huart2.Init.WordLength = UART_WORDLENGTH_8B;
    huart2.Init.StopBits = UART_STOPBITS_1;
    huart2.Init.Parity = UART_PARITY_NONE;
    huart2.Init.Mode = UART_MODE_TX_RX;
    huart2.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart2.Init.OverSampling = UART_OVERSAMPLING_8;
    require_hal(HAL_UART_Init(&huart2));

    hdma_usart2_tx.Instance = DMA1_Stream6;
    hdma_usart2_tx.Init.Channel = DMA_CHANNEL_4;
    hdma_usart2_tx.Init.Direction = DMA_MEMORY_TO_PERIPH;
    hdma_usart2_tx.Init.PeriphInc = DMA_PINC_DISABLE;
    hdma_usart2_tx.Init.MemInc = DMA_MINC_ENABLE;
    hdma_usart2_tx.Init.PeriphDataAlignment = DMA_PDATAALIGN_BYTE;
    hdma_usart2_tx.Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
    hdma_usart2_tx.Init.Mode = DMA_NORMAL;
    hdma_usart2_tx.Init.Priority = DMA_PRIORITY_MEDIUM;
    hdma_usart2_tx.Init.FIFOMode = DMA_FIFOMODE_DISABLE;
    require_hal(HAL_DMA_Init(&hdma_usart2_tx));
    __HAL_LINKDMA(&huart2, hdmatx, hdma_usart2_tx);
    HAL_NVIC_SetPriority(USART2_IRQn, 6, 0);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

void DMA2_Stream0_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_adc1); }
void DMA1_Stream5_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_dac1); }
void DMA1_Stream6_IRQHandler(void) { HAL_DMA_IRQHandler(&hdma_usart2_tx); }
void ADC_IRQHandler(void) { HAL_ADC_IRQHandler(&hadc1); }
void USART2_IRQHandler(void) { HAL_UART_IRQHandler(&huart2); }

void SysTick_Handler(void) {
    HAL_IncTick();
    if (xTaskGetSchedulerState() != taskSCHEDULER_NOT_STARTED) {
        xPortSysTickHandler();
    }
}
