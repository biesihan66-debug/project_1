/*
 * TIM3 同步触发 ADC1（PA6）和 ADC2（PC4）。DMA1 Stream0 从 ADC12_CDR
 * 搬运到 D2 SRAM 中的 packed_adc_samples[i]。低 16 位为电压，高 16 位为 TIA。
 * 使用 16 位 ADC，保留原采样帧格式。下一次采集覆盖缓冲区。
 * 帧交给 RLC_Calculate.c 解调，计算值进 OLED 第 2 行，采样错误进第 4 行。
 */
#include "ADC_Sample.h"
#include "BSP_Board.h"
#include "BSP_PinMap.h"
#include "delay.h"
#include "stm32h7xx_ll_adc.h"
#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_dma.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_tim.h"

#define ADC_MAX_SAMPLE_PAIRS 2048u
#define ADC_DMA_STREAM LL_DMA_STREAM_0

/* D2 SRAM 可由 DMA1 访问。本工程保持 D-Cache 关闭。 */
static uint32_t packed_adc_samples[ADC_MAX_SAMPLE_PAIRS]
    __attribute__((section(".bss.adc_samples"), aligned(32), zero_init));

static bool CalibrateADC(ADC_TypeDef *adc, uint32_t channel, uint32_t boost)
{
    LL_ADC_SetResolution(adc, LL_ADC_RESOLUTION_16B);
    LL_ADC_SetChannelSingleDiff(adc, channel, LL_ADC_SINGLE_ENDED);
    LL_ADC_SetChannelPreselection(adc, channel);
    LL_ADC_REG_SetSequencerLength(adc, LL_ADC_REG_SEQ_SCAN_DISABLE);
    LL_ADC_REG_SetSequencerRanks(adc, LL_ADC_REG_RANK_1, channel);
    /* ES0392 §2.9.6：关闭 DR FIFO，避免双 ADC 从路错位。 */
    LL_ADC_REG_SetOverrun(adc, LL_ADC_REG_OVR_DATA_OVERWRITTEN);
    LL_ADC_SetBoostMode(adc, boost);
    LL_ADC_StartCalibration(adc, LL_ADC_CALIB_OFFSET_LINEARITY, LL_ADC_SINGLE_ENDED);
    /* DS12110：线性和偏移校准约 165010 周期，15 MHz 时约 11 ms。 */
    const uint32_t deadline_ms = BSP_GetMillis() + 25u;
    while (LL_ADC_IsCalibrationOnGoing(adc) != 0u)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    }
    return true;
}

static bool EnableADC(ADC_TypeDef *adc)
{
    LL_ADC_ClearFlag_ADRDY(adc);
    LL_ADC_Enable(adc);
    const uint32_t deadline_ms = BSP_GetMillis() + 3u;
    while (LL_ADC_IsActiveFlag_ADRDY(adc) == 0u)
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
    LL_GPIO_InitTypeDef gpio = {0};
    /* HCLK /4。Y/W 的 ADC 时钟为 30 MHz，Rev V 在模拟侧再除以 2。 */
    const uint32_t revision = DBGMCU->IDCODE >> DBGMCU_IDCODE_REV_ID_Pos;
    const uint32_t boost = revision <= 0x1003u ? LL_ADC_BOOST_MODE_50MHZ : LL_ADC_BOOST_MODE_20MHZ;
    if (revision == 0x1003u)
    {
        /* ES0392 §2.9.7：此修订版同时转换可能影响精度，串口提示实板验收。 */
        BSP_DebugWrite("ADC rev=0x1003 dual_accuracy=check\r\n");
    }

    gpio.Mode = LL_GPIO_MODE_ANALOG;
    gpio.Pin = RLC_ADC_DUT_PIN;
    LL_GPIO_Init(RLC_ADC_DUT_GPIO, &gpio);
    gpio.Pin = RLC_ADC_TIA_PIN;
    LL_GPIO_Init(RLC_ADC_TIA_GPIO, &gpio);

    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_ADC12 | LL_AHB1_GRP1_PERIPH_DMA1);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);
    LL_ADC_SetCommonClock(ADC12_COMMON, LL_ADC_CLOCK_SYNC_PCLK_DIV4);
    LL_ADC_DisableDeepPowerDown(ADC1);
    LL_ADC_DisableDeepPowerDown(ADC2);
    LL_ADC_EnableInternalRegulator(ADC1);
    LL_ADC_EnableInternalRegulator(ADC2);
    DelayUs(LL_ADC_DELAY_INTERNAL_REGUL_STAB_US);
    if (!CalibrateADC(ADC1, RLC_ADC_DUT_CHANNEL, boost) ||
        !CalibrateADC(ADC2, RLC_ADC_TIA_CHANNEL, boost))
    {
        return false;
    }
    DelayUs(1u); /* 等待校准后的最少 ADC 周期。 */

    LL_ADC_SetMultimode(ADC12_COMMON, LL_ADC_MULTI_DUAL_REG_SIMULT);
    LL_ADC_SetMultiDMATransfer(ADC12_COMMON, LL_ADC_MULTI_REG_DMA_RES_32_10B);
    /* Each TIM3 edge triggers a one-rank sequence. Keep master DMA requests
     * enabled across edges. DMA1 itself stops at the programmed frame length. */
    LL_ADC_REG_SetDataTransferMode(ADC1, LL_ADC_REG_DMA_TRANSFER_UNLIMITED);
    LL_ADC_REG_SetTriggerSource(ADC1, LL_ADC_REG_TRIG_EXT_TIM3_TRGO);
    if (!EnableADC(ADC1) || !EnableADC(ADC2))
    {
        return false;
    }

    LL_DMA_ConfigTransfer(DMA1, ADC_DMA_STREAM,
                         LL_DMA_DIRECTION_PERIPH_TO_MEMORY | LL_DMA_MODE_NORMAL |
                         LL_DMA_PERIPH_NOINCREMENT | LL_DMA_MEMORY_INCREMENT |
                         LL_DMA_PDATAALIGN_WORD | LL_DMA_MDATAALIGN_WORD | LL_DMA_PRIORITY_HIGH);
    LL_DMA_SetPeriphRequest(DMA1, ADC_DMA_STREAM, LL_DMAMUX1_REQ_ADC1);
    /* 双 ADC 打包数据在公共 CDR 寄存器，DMA 需要其地址。 */
    LL_DMA_SetPeriphAddress(DMA1, ADC_DMA_STREAM, (uint32_t)(uintptr_t)&ADC12_COMMON->CDR);
    LL_DMA_SetMemoryAddress(DMA1, ADC_DMA_STREAM, (uint32_t)(uintptr_t)packed_adc_samples);
    return true;
}

static bool StopAcquisition(void)
{
    LL_TIM_DisableCounter(TIM3);
    if (LL_ADC_REG_IsConversionOngoing(ADC1) != 0u)
    {
        LL_ADC_REG_StopConversion(ADC1);
    }
    LL_DMA_DisableStream(DMA1, ADC_DMA_STREAM);
    const uint32_t deadline_ms = BSP_GetMillis() + 3u;
    while (LL_ADC_REG_IsConversionOngoing(ADC1) != 0u ||
           LL_ADC_REG_IsConversionOngoing(ADC2) != 0u ||
           LL_DMA_IsEnabledStream(DMA1, ADC_DMA_STREAM) != 0u)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    }
    return true;
}

static void StartAcquisition(uint32_t timer_ticks, uint16_t pairs, uint32_t sample_time)
{
    LL_ADC_SetChannelSamplingTime(ADC1, RLC_ADC_DUT_CHANNEL, sample_time);
    LL_ADC_SetChannelSamplingTime(ADC2, RLC_ADC_TIA_CHANNEL, sample_time);
    LL_ADC_ClearFlag_OVR(ADC1);
    LL_ADC_ClearFlag_OVR(ADC2);
    LL_ADC_ClearFlag_EOC(ADC1);
    LL_ADC_ClearFlag_EOC(ADC2);
    LL_ADC_ClearFlag_EOS(ADC1);
    LL_ADC_ClearFlag_EOS(ADC2);
    LL_TIM_SetAutoReload(TIM3, timer_ticks - 1u);
    LL_TIM_GenerateEvent_UPDATE(TIM3);
    LL_TIM_ClearFlag_UPDATE(TIM3);
    LL_TIM_SetTriggerOutput(TIM3, LL_TIM_TRGO_UPDATE);

    LL_DMA_ClearFlag_TC0(DMA1);
    LL_DMA_ClearFlag_HT0(DMA1);
    LL_DMA_ClearFlag_TE0(DMA1);
    LL_DMA_ClearFlag_DME0(DMA1);
    LL_DMA_ClearFlag_FE0(DMA1);
    LL_DMA_SetDataLength(DMA1, ADC_DMA_STREAM, pairs);
    LL_DMA_EnableStream(DMA1, ADC_DMA_STREAM);
    LL_ADC_REG_StartConversion(ADC1);
    LL_TIM_EnableCounter(TIM3);
}

static RLC_Status WaitForSamples(uint32_t deadline_ms, RLC_Frame *frame)
{
    while (BSP_BeforeDeadline(deadline_ms))
    {
        if (LL_DMA_IsActiveFlag_TE0(DMA1) != 0u || LL_DMA_IsActiveFlag_DME0(DMA1) != 0u ||
            LL_DMA_IsActiveFlag_FE0(DMA1) != 0u)
        {
            frame->fault = "dma_transfer";
            return RLC_STATUS_HARDWARE_ERROR;
        }
        /* DMA 填满后，定时器停下前的新样本可置 OVR，已经搬完的帧仍有效。 */
        if (LL_DMA_IsActiveFlag_TC0(DMA1) != 0u)
        {
            return RLC_STATUS_OK;
        }
        if (LL_ADC_IsActiveFlag_OVR(ADC1) != 0u || LL_ADC_IsActiveFlag_OVR(ADC2) != 0u)
        {
            frame->fault = "adc_overrun";
            return RLC_STATUS_HARDWARE_ERROR;
        }
    }
    frame->fault = "adc_wait";
    return RLC_STATUS_TIMEOUT;
}

static bool NearRail(uint16_t code)
{
    return code <= RLC_ADC_LOW_RAIL_CODE || code >= RLC_ADC_HIGH_RAIL_CODE;
}

RLC_Status ADC_Capture(uint32_t nominal_hz, float actual_hz, uint8_t cycles, uint32_t deadline_ms,
                       RLC_Frame *frame)
{
    uint32_t points_per_cycle = 60u;
    uint32_t sample_time = LL_ADC_SAMPLINGTIME_64CYCLES_5;
    if (nominal_hz == 100000u)
    {
        points_per_cycle = 5u;
        const uint32_t revision = DBGMCU->IDCODE >> DBGMCU_IDCODE_REV_ID_Pos;
        /* 30 MHz 与 15 MHz 硅版本均保留约 1.1 µs 采样时间。 */
        sample_time = revision <= 0x1003u ? LL_ADC_SAMPLINGTIME_32CYCLES_5
                                          : LL_ADC_SAMPLINGTIME_16CYCLES_5;
    }
    else if (nominal_hz == 10000u)
    {
        points_per_cycle = 30u;
        sample_time = LL_ADC_SAMPLINGTIME_32CYCLES_5;
    }
    if (frame == NULL)
    {
        return RLC_STATUS_HARDWARE_ERROR;
    }
    frame->fault = NULL;
    if (nominal_hz == 0u || !(actual_hz > 0.0f) || cycles == 0u ||
        nominal_hz > RLC_TIM3_CLOCK_HZ / points_per_cycle)
    {
        frame->fault = "adc_config";
        return RLC_STATUS_HARDWARE_ERROR;
    }
    if (!BSP_BeforeDeadline(deadline_ms))
    {
        return RLC_STATUS_TIMEOUT;
    }

    /* 6 / 60 / 300 / 500 k 点对每秒；100 kHz 保留 16 位转换和触发余量。 */
    const uint32_t timer_ticks = RLC_TIM3_CLOCK_HZ / (nominal_hz * points_per_cycle);
    if (timer_ticks > 65536u || !StopAcquisition())
    {
        frame->fault = "adc_prepare";
        return RLC_STATUS_HARDWARE_ERROR;
    }
    const uint32_t max_cycles = ADC_MAX_SAMPLE_PAIRS / points_per_cycle;
    const uint16_t pairs = (uint16_t)((cycles > max_cycles ? max_cycles : cycles) * points_per_cycle);
    if (!BSP_BeforeDeadline(deadline_ms))
    {
        return RLC_STATUS_TIMEOUT;
    }
    StartAcquisition(timer_ticks, pairs, sample_time);
    const RLC_Status status = WaitForSamples(deadline_ms, frame);
    if (!StopAcquisition())
    {
        frame->fault = "adc_stop";
        return RLC_STATUS_HARDWARE_ERROR;
    }
    if (status != RLC_STATUS_OK)
    {
        return status;
    }
    __DMB();
    frame->packed_samples = packed_adc_samples;
    frame->sample_count = pairs;
    frame->cycles_per_sample = actual_hz * ((float)timer_ticks / (float)RLC_TIM3_CLOCK_HZ);
    frame->dut_volts_per_code = RLC_ADC_DUT_VOLTS_PER_CODE;
    frame->tia_volts_per_code = RLC_ADC_TIA_VOLTS_PER_CODE;
    frame->dut_clipped = false;
    frame->tia_clipped = false;
    for (uint16_t i = 0u; i < pairs; ++i)
    {
        const uint32_t packed = packed_adc_samples[i];
        frame->dut_clipped |= NearRail((uint16_t)packed);
        frame->tia_clipped |= NearRail((uint16_t)(packed >> 16));
    }
    return RLC_STATUS_OK;
}
