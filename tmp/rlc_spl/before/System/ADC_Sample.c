/*
 * TIM3 触发 ADC1、ADC2 双规则同步采样。DMA1 通道 1 把每对采样写到 SRAM
 * 的 packed_adc_samples[i]，低 16 位为电压，高 16 位为跨阻输出。
 * ADC 引脚、标度和触轨阈值在 BSP_PinMap.h。数组由下一次采样覆盖。
 * 采样结果供阻抗解算，所得元件值进入 OLED 第 2 行，过载进入第 4 行。
 */
#include "BSP_PinMap.h"
#include "ADC_Sample.h"
#include "BSP_Board.h"

#define ADC_SR(base)    STM32_REG32((base) + 0x00u)
#define ADC_CR1(base)   STM32_REG32((base) + 0x04u)
#define ADC_CR2(base)   STM32_REG32((base) + 0x08u)
#define ADC_SMPR1(base) STM32_REG32((base) + 0x0Cu)
#define ADC_SMPR2(base) STM32_REG32((base) + 0x10u)
#define ADC_SQR1(base)  STM32_REG32((base) + 0x2Cu)
#define ADC_SQR3(base)  STM32_REG32((base) + 0x34u)
#define ADC_DR(base)    STM32_REG32((base) + 0x4Cu)

#define ADC_ADON             (1u << 0)
#define ADC_CAL              (1u << 2)
#define ADC_RSTCAL           (1u << 3)
#define ADC_DMA              (1u << 8)
#define ADC_EXTSEL_TIM3_TRGO (4u << 17)
#define ADC_EXTSEL_SOFTWARE  (7u << 17)
#define ADC_EXTTRIG          (1u << 20)
#define ADC_DUAL_REGULAR_SIM (6u << 16)

#define DMA_ISR          STM32_REG32(STM32_DMA1 + 0x00u)
#define DMA_IFCR         STM32_REG32(STM32_DMA1 + 0x04u)
#define DMA_CH1_CCR      STM32_REG32(STM32_DMA1 + 0x08u)
#define DMA_CH1_CNDTR    STM32_REG32(STM32_DMA1 + 0x0Cu)
#define DMA_CH1_CPAR     STM32_REG32(STM32_DMA1 + 0x10u)
#define DMA_CH1_CMAR     STM32_REG32(STM32_DMA1 + 0x14u)
#define DMA_CH1_COMPLETE (1u << 1)
#define DMA_CH1_ERROR    (1u << 3)
#define DMA_CH1_CONFIG   ((2u << 12) | (2u << 10) | (2u << 8) | (1u << 7))

#define TIM3_CR1 STM32_REG32(STM32_TIM3 + 0x00u)
#define TIM3_CR2 STM32_REG32(STM32_TIM3 + 0x04u)
#define TIM3_SR  STM32_REG32(STM32_TIM3 + 0x10u)
#define TIM3_EGR STM32_REG32(STM32_TIM3 + 0x14u)
#define TIM3_PSC STM32_REG32(STM32_TIM3 + 0x28u)
#define TIM3_ARR STM32_REG32(STM32_TIM3 + 0x2Cu)

#define ADC_MAX_SAMPLE_PAIRS 2048u

/* 以 32 位对齐保存两路数据，采完只读。2048 对占 8192 字节。 */
static uint32_t packed_adc_samples[ADC_MAX_SAMPLE_PAIRS];

static bool CalibrateADC(uint32_t base)
{
    uint32_t deadline_ms = BSP_GetMillis() + 5u;

    ADC_CR2(base) |= ADC_RSTCAL;
    while ((ADC_CR2(base) & ADC_RSTCAL) != 0u)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    }
    ADC_CR2(base) |= ADC_CAL;
    while ((ADC_CR2(base) & ADC_CAL) != 0u)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    }
    return true;
}

static void SetADCSampleTime(uint32_t base, uint8_t channel, uint8_t sample_time)
{
    uint32_t shift = (uint32_t)(channel % 10u) * 3u;
    volatile uint32_t *sampling = channel < 10u ? &ADC_SMPR2(base) : &ADC_SMPR1(base);

    *sampling = (*sampling & ~(7u << shift)) | ((uint32_t)sample_time << shift);
}

bool ADC_Init(void)
{
    uint32_t ready_ms;

    STM32_RCC_AHBENR |= 1u;
    STM32_RCC_APB2ENR |= (1u << 9) | (1u << 10);
    STM32_RCC_APB1ENR |= 1u << 1;
    BSP_GPIO_Config(RLC_ADC_GPIO, RLC_ADC_DUT_PIN, STM32_GPIO_ANALOG_IN);
    BSP_GPIO_Config(RLC_ADC_GPIO, RLC_ADC_TIA_PIN, STM32_GPIO_ANALOG_IN);

    ADC_CR2(STM32_ADC1) = ADC_ADON;
    ADC_CR2(STM32_ADC2) = ADC_ADON;
    ready_ms = BSP_GetMillis() + 2u;
    while (BSP_BeforeDeadline(ready_ms))
    {
        /* ADC 上电后需稳定，等待两个 SysTick 边沿再校准。 */
    }
    if (!CalibrateADC(STM32_ADC1) || !CalibrateADC(STM32_ADC2))
    {
        return false;
    }

    ADC_SQR1(STM32_ADC1) = 0u;
    ADC_SQR1(STM32_ADC2) = 0u;
    ADC_SQR3(STM32_ADC1) = RLC_ADC_DUT_CHANNEL;
    ADC_SQR3(STM32_ADC2) = RLC_ADC_TIA_CHANNEL;
    ADC_CR1(STM32_ADC1) = ADC_DUAL_REGULAR_SIM;
    ADC_CR2(STM32_ADC1) = ADC_ADON | ADC_EXTSEL_TIM3_TRGO;
    ADC_CR2(STM32_ADC2) = ADC_ADON | ADC_EXTSEL_SOFTWARE | ADC_EXTTRIG;
    TIM3_CR2 = 2u << 4; /* 更新事件输出 TRGO。 */
    return true;
}

static void StopAcquisition(void)
{
    TIM3_CR1 = 0u;
    ADC_CR2(STM32_ADC1) &= ~(ADC_EXTTRIG | ADC_DMA);
    DMA_CH1_CCR = 0u;
    DMA_IFCR = 0xFu;
}

static void StartAcquisition(uint32_t timer_ticks, uint16_t pairs, uint8_t sample_time)
{
    SetADCSampleTime(STM32_ADC1, RLC_ADC_DUT_CHANNEL, sample_time);
    SetADCSampleTime(STM32_ADC2, RLC_ADC_TIA_CHANNEL, sample_time);
    ADC_SR(STM32_ADC1) = 0u;
    ADC_SR(STM32_ADC2) = 0u;
    TIM3_PSC = 0u;
    TIM3_ARR = timer_ticks - 1u;
    TIM3_EGR = 1u; /* 先更新定时器，再开启 ADC 外部触发。 */
    TIM3_SR = 0u;

    DMA_CH1_CPAR = (uint32_t)(uintptr_t)&ADC_DR(STM32_ADC1);
    DMA_CH1_CMAR = (uint32_t)(uintptr_t)packed_adc_samples;
    DMA_CH1_CNDTR = pairs;
    DMA_CH1_CCR = DMA_CH1_CONFIG | 1u;
    ADC_CR2(STM32_ADC1) |= ADC_EXTTRIG | ADC_DMA;
    TIM3_CR1 = 1u;
}

static bool NearRail(uint16_t code)
{
    return code <= RLC_ADC_LOW_RAIL_CODE || code >= RLC_ADC_HIGH_RAIL_CODE;
}

RLC_Status ADC_Capture(uint32_t nominal_hz, float actual_hz, uint8_t cycles, uint32_t deadline_ms,
                       RLC_Frame *frame)
{
    uint8_t points_per_cycle;
    uint8_t sample_time;
    uint32_t timer_ticks;
    uint16_t pairs;
    uint32_t dma_state;
    uint16_t i;

    if (nominal_hz == 0u || actual_hz <= 0.0f || cycles == 0u || frame == NULL)
    {
        return RLC_STATUS_HARDWARE_ERROR;
    }
    if (!BSP_BeforeDeadline(deadline_ms))
    {
        return RLC_STATUS_TIMEOUT;
    }

    /* 按频段配置采样建立时间。600 k 对/秒使用最短时间，要求低阻抗驱动。 */
    if (nominal_hz == 100000u)
    {
        points_per_cycle = 6u;
        sample_time = 0u; /* 1.5 ADC 周期。 */
    }
    else if (nominal_hz == 10000u)
    {
        points_per_cycle = 30u;
        sample_time = 2u; /* 13.5 ADC 周期。 */
    }
    else
    {
        points_per_cycle = 60u;
        sample_time = 5u; /* 55.5 ADC 周期。 */
    }
    timer_ticks = RLC_TIM3_CLOCK_HZ / (nominal_hz * points_per_cycle);
    if ((uint32_t)cycles * points_per_cycle > ADC_MAX_SAMPLE_PAIRS)
    {
        cycles = (uint8_t)(ADC_MAX_SAMPLE_PAIRS / points_per_cycle);
    }
    pairs = (uint16_t)((uint32_t)cycles * points_per_cycle);
    StopAcquisition();
    StartAcquisition(timer_ticks, pairs, sample_time);

    do
    {
        dma_state = DMA_ISR;
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            StopAcquisition();
            return RLC_STATUS_TIMEOUT;
        }
    } while ((dma_state & (DMA_CH1_COMPLETE | DMA_CH1_ERROR)) == 0u);
    StopAcquisition();
    if ((dma_state & DMA_CH1_ERROR) != 0u)
    {
        return RLC_STATUS_HARDWARE_ERROR;
    }

    frame->packed_samples = packed_adc_samples;
    frame->sample_count = pairs;
    frame->cycles_per_sample = actual_hz * ((float)timer_ticks / (float)RLC_TIM3_CLOCK_HZ);
    frame->dut_volts_per_code = RLC_ADC_DUT_VOLTS_PER_CODE;
    frame->tia_volts_per_code = RLC_ADC_TIA_VOLTS_PER_CODE;
    frame->dut_clipped = false;
    frame->tia_clipped = false;
    for (i = 0u; i < pairs; ++i)
    {
        uint32_t packed = packed_adc_samples[i];

        frame->dut_clipped |= NearRail((uint16_t)packed);
        frame->tia_clipped |= NearRail((uint16_t)(packed >> 16));
    }
    return RLC_STATUS_OK;
}
