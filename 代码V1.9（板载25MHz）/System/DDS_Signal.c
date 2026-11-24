/*
 * AD9833 正弦源控制。SPI1 接线和 模块板载 25 MHz MCLK，PA8 留空。
 * excitation_hz 为实际调谐字换算的 Hz，由采样模块读取以计算每点相位增量。
 * 屏幕第 3 行显示选择的标称测试频率，TP1 处用示波器核对频率和电平。
 */
#include "BSP_PinMap.h"
#include "DDS_Signal.h"
#include "BSP_Board.h"
#include "delay.h"
#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_rcc.h"
#include "stm32h7xx_ll_spi.h"

#define AD9833_B28         0x2000u
#define AD9833_RESET       0x0100u
#define AD9833_FREQUENCY_0 0x4000u
#define AD9833_PHASE_0     0xC000u
#define AD9833_DATA_MASK   0x3FFFu
#define AD9833_ACCUMULATOR (1u << 28)

/* 只有成功发送完整调谐字并解除复位后，才供采样使用。 */
static float excitation_hz;

static void InitSPI(void)
{
    LL_APB2_GRP1_ForceReset(LL_APB2_GRP1_PERIPH_SPI1);
    LL_APB2_GRP1_ReleaseReset(LL_APB2_GRP1_PERIPH_SPI1);
    LL_SPI_SetMode(SPI1, LL_SPI_MODE_MASTER);
    LL_SPI_SetTransferDirection(SPI1, LL_SPI_SIMPLEX_TX);
    LL_SPI_SetDataWidth(SPI1, LL_SPI_DATAWIDTH_16BIT);
    LL_SPI_SetBaudRatePrescaler(SPI1, LL_SPI_BAUDRATEPRESCALER_DIV128);
    LL_SPI_SetClockPolarity(SPI1, LL_SPI_POLARITY_HIGH);
    LL_SPI_SetClockPhase(SPI1, LL_SPI_PHASE_1EDGE);
    LL_SPI_SetNSSMode(SPI1, LL_SPI_NSS_SOFT);
    LL_SPI_SetInternalSSLevel(SPI1, LL_SPI_SS_LEVEL_HIGH);
    LL_SPI_EnableGPIOControl(SPI1);
    LL_SPI_SetTransferSize(SPI1, 1u);
    LL_SPI_Enable(SPI1);
}

static bool WriteDDSWord(uint16_t word, uint32_t deadline_ms)
{
    if (!BSP_BeforeDeadline(deadline_ms))
    {
        return false;
    }

    LL_GPIO_ResetOutputPin(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN);
    LL_SPI_TransmitData16(SPI1, word);
    LL_SPI_StartMasterTransfer(SPI1);
    while (LL_SPI_IsActiveFlag_EOT(SPI1) == 0u)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            LL_GPIO_SetOutputPin(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN);
            InitSPI();
            return false;
        }
    }
    /* 等最后半个 SCLK（0.8 us）结束，再拉高 FSYNC。 */
    DelayUs(1u);
    LL_GPIO_SetOutputPin(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN);
    LL_SPI_ClearFlag_EOT(SPI1);
    LL_SPI_ClearFlag_TXTF(SPI1);
    /* RM0433 建议逐帧复位 SPI 状态机。最后的 SCLK 已完整输出。 */
    LL_SPI_Disable(SPI1);
    LL_SPI_SetTransferSize(SPI1, 1u);
    LL_SPI_Enable(SPI1);
    /* ES0392 §2.22.2：距 EOT 至少等一个 SCLK（1.6 us）再启动。 */
    DelayUs(1u);
    return BSP_BeforeDeadline(deadline_ms);
}

bool DDS_Init(void)
{
    LL_GPIO_InitTypeDef gpio = {0};

    LL_GPIO_SetOutputPin(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN);
    gpio.Pin = RLC_DDS_SELECT_PIN;
    gpio.Mode = LL_GPIO_MODE_OUTPUT;
    gpio.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio.Speed = LL_GPIO_SPEED_FREQ_LOW;
    LL_GPIO_Init(RLC_DDS_GPIO, &gpio);
    gpio.Pin = RLC_DDS_CLOCK_PIN | RLC_DDS_DATA_PIN;
    gpio.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = LL_GPIO_AF_5;
    LL_GPIO_Init(RLC_DDS_GPIO, &gpio);
#if RLC_AD9833_USE_MCO
    LL_RCC_ConfigMCO(LL_RCC_MCO1SOURCE_PLL1QCLK, LL_RCC_MCO1_DIV_10);
    gpio.Pin = RLC_DDS_MCO_PIN;
    gpio.Alternate = LL_GPIO_AF_0;
    LL_GPIO_Init(RLC_DDS_GPIO, &gpio);
#endif
    /* 16 位 MSB 优先，首个下降沿采样。80 MHz / 128 = 625 kHz，给 FSYNC 留出时序余量。 */
    LL_RCC_SetSPIClockSource(LL_RCC_SPI123_CLKSOURCE_PLL1Q);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SPI1);
    InitSPI();
    return DDS_Stop(BSP_GetMillis() + 5u);
}

bool DDS_Stop(uint32_t deadline_ms)
{
    excitation_hz = 0.0f;
    return WriteDDSWord(AD9833_B28 | AD9833_RESET, deadline_ms);
}

bool DDS_SetFrequency(uint32_t nominal_hz, uint32_t deadline_ms)
{
    uint32_t tuning_word =
        (uint32_t)(((uint64_t)nominal_hz * AD9833_ACCUMULATOR + RLC_AD9833_MCLK_HZ / 2u) /
                   RLC_AD9833_MCLK_HZ);

    if (!WriteDDSWord((uint16_t)(AD9833_FREQUENCY_0 | (tuning_word & AD9833_DATA_MASK)),
                      deadline_ms) ||
        !WriteDDSWord((uint16_t)(AD9833_FREQUENCY_0 | (tuning_word >> 14)), deadline_ms) ||
        !WriteDDSWord(AD9833_PHASE_0, deadline_ms) || !WriteDDSWord(AD9833_B28, deadline_ms))
    {
        return false;
    }
    excitation_hz = (float)tuning_word * ((float)RLC_AD9833_MCLK_HZ / (float)AD9833_ACCUMULATOR);
    return true;
}

float DDS_GetActualFrequencyHz(void)
{
    return excitation_hz;
}
