/*
 * TIM3 触发 ADC1、ADC2 双规则同步采样。DMA1 通道 1 把每对采样写到 SRAM
 * 的 packed_adc_samples[i]，低 16 位为电压，高 16 位为跨阻输出。
 * ADC 引脚、标度和触轨阈值在 BSP_PinMap.h。数组由下一次采样覆盖。
 * 采样结果供阻抗解算，所得元件值进入 OLED 第 2 行，过载进入第 4 行。
 */
#include "ADC_Sample.h"
#include "stm32f10x.h"
#include "BSP_PinMap.h"
#include "BSP_Board.h"
#include "delay.h"

#define ADC_MAX_SAMPLE_PAIRS 2048u
#define ADC_FRAME_FLAGS (ADC_FLAG_AWD | ADC_FLAG_EOC | ADC_FLAG_JEOC | \
                         ADC_FLAG_JSTRT | ADC_FLAG_STRT)

/* 以 32 位对齐保存两路数据，采完只读。2048 对占 8192 字节。 */
static uint32_t packed_adc_samples[ADC_MAX_SAMPLE_PAIRS];

static bool CalibrateADC(ADC_TypeDef *adc)
{
    uint32_t deadline_ms = BSP_GetMillis() + 5u;

    ADC_ResetCalibration(adc);
    while (ADC_GetResetCalibrationStatus(adc) == SET)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    }
    ADC_StartCalibration(adc);
    while (ADC_GetCalibrationStatus(adc) == SET)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    }
    return true;
}

bool BSP_ADC_Init(void)
{
    GPIO_InitTypeDef gpio_init;
    ADC_InitTypeDef adc_init;

    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);
    DMA_DeInit(DMA1_Channel1);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_ADC2, ENABLE);
    ADC_DeInit(ADC1);
    ADC_DeInit(ADC2);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    TIM_DeInit(TIM3);
    gpio_init.GPIO_Pin = RLC_ADC_DUT_PIN | RLC_ADC_TIA_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_AIN;
    gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(RLC_ADC_GPIO, &gpio_init);

    adc_init.ADC_ScanConvMode = DISABLE;
    adc_init.ADC_ContinuousConvMode = DISABLE;
    adc_init.ADC_DataAlign = ADC_DataAlign_Right;
    adc_init.ADC_NbrOfChannel = 1u;

    /* ADC1 是主机，TIM3 更新事件触发一对转换。 */
    adc_init.ADC_Mode = ADC_Mode_RegSimult;
    adc_init.ADC_ExternalTrigConv = ADC_ExternalTrigConv_T3_TRGO;
    ADC_Init(ADC1, &adc_init);
    ADC_RegularChannelConfig(ADC1, RLC_ADC_DUT_CHANNEL, 1u, ADC_SampleTime_1Cycles5);

    /* ADC2 是从机，规则组由 ADC1 同步启动。 */
    adc_init.ADC_Mode = ADC_Mode_Independent;
    adc_init.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_Init(ADC2, &adc_init);
    ADC_RegularChannelConfig(ADC2, RLC_ADC_TIA_CHANNEL, 1u, ADC_SampleTime_1Cycles5);

    ADC_Cmd(ADC1, ENABLE);
    ADC_Cmd(ADC2, ENABLE);
    Delay(2u); /* ADC 上电稳定后再校准。 */
    if (!CalibrateADC(ADC1) || !CalibrateADC(ADC2))
    {
        return false;
    }

    ADC_ExternalTrigConvCmd(ADC2, ENABLE);
    TIM_SelectOutputTrigger(TIM3, TIM_TRGOSource_Update);
    return true;
}

static void StopAcquisition(void)
{
    TIM_Cmd(TIM3, DISABLE);
    ADC_ExternalTrigConvCmd(ADC1, DISABLE);
    ADC_DMACmd(ADC1, DISABLE);
    DMA_Cmd(DMA1_Channel1, DISABLE);
    DMA_ClearFlag(DMA1_FLAG_GL1);
}

static void StartAcquisition(uint32_t timer_ticks, uint16_t pairs, uint8_t sample_time)
{
    DMA_InitTypeDef dma_init;

    ADC_RegularChannelConfig(ADC1, RLC_ADC_DUT_CHANNEL, 1u, sample_time);
    ADC_RegularChannelConfig(ADC2, RLC_ADC_TIA_CHANNEL, 1u, sample_time);
    ADC_ClearFlag(ADC1, ADC_FRAME_FLAGS);
    ADC_ClearFlag(ADC2, ADC_FRAME_FLAGS);
    TIM_SetAutoreload(TIM3, (uint16_t)(timer_ticks - 1u));
    /* 先加载分频和周期，再开启 ADC 外部触发，配置时的 UG 不计作第一个样点。 */
    TIM_PrescalerConfig(TIM3, 0u, TIM_PSCReloadMode_Immediate);
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);

    /* SPL 需要 DMA 源地址。取 ADC1->DR 的地址，DMA 以 32 位搬运两路结果。 */
    dma_init.DMA_PeripheralBaseAddr = (uint32_t)(uintptr_t)&ADC1->DR;
    dma_init.DMA_MemoryBaseAddr = (uint32_t)(uintptr_t)packed_adc_samples;
    dma_init.DMA_DIR = DMA_DIR_PeripheralSRC;
    dma_init.DMA_BufferSize = pairs;
    dma_init.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    dma_init.DMA_MemoryInc = DMA_MemoryInc_Enable;
    dma_init.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Word;
    dma_init.DMA_MemoryDataSize = DMA_MemoryDataSize_Word;
    dma_init.DMA_Mode = DMA_Mode_Normal;
    dma_init.DMA_Priority = DMA_Priority_High;
    dma_init.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel1, &dma_init);
    DMA_Cmd(DMA1_Channel1, ENABLE);
    ADC_DMACmd(ADC1, ENABLE);
    ADC_ExternalTrigConvCmd(ADC1, ENABLE);
    TIM_Cmd(TIM3, ENABLE);
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
    bool dma_error;
    bool dma_complete;
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
        sample_time = ADC_SampleTime_1Cycles5;
    }
    else if (nominal_hz == 10000u)
    {
        points_per_cycle = 30u;
        sample_time = ADC_SampleTime_13Cycles5;
    }
    else
    {
        points_per_cycle = 60u;
        sample_time = ADC_SampleTime_55Cycles5;
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
        dma_complete = DMA_GetFlagStatus(DMA1_FLAG_TC1) == SET;
        dma_error = DMA_GetFlagStatus(DMA1_FLAG_TE1) == SET;
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            StopAcquisition();
            return RLC_STATUS_TIMEOUT;
        }
    } while (!dma_complete && !dma_error);
    StopAcquisition();
    if (dma_error)
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
