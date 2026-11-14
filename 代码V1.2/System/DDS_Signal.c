/*
 * AD9833 正弦源控制。SPI1 的接线和 MCLK 来自 BSP_PinMap.h。
 * excitation_hz 为实际调谐字换算的 Hz，由采样模块读取以计算每点相位增量。
 * 屏幕第 3 行显示选择的标称测试频率，TP1 处用示波器核对频率和电平。
 */
#include "stm32f10x.h"
#include "BSP_PinMap.h"
#include "DDS_Signal.h"
#include "BSP_Board.h"

#define AD9833_B28         0x2000u
#define AD9833_RESET       0x0100u
#define AD9833_FREQUENCY_0 0x4000u
#define AD9833_PHASE_0     0xC000u
#define AD9833_DATA_MASK   0x3FFFu
#define AD9833_ACCUMULATOR (1u << 28)

/* 只有成功发送完整调谐字并解除复位后，才供采样使用。 */
static float excitation_hz;

static bool SpiWait(uint16_t flag, FlagStatus expected, uint32_t deadline_ms)
{
    while (SPI_I2S_GetFlagStatus(SPI1, flag) != expected)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    }
    return true;
}

static bool WriteDDSWord(uint16_t word, uint32_t deadline_ms)
{
    bool complete;

    if (!BSP_BeforeDeadline(deadline_ms) || !SpiWait(SPI_I2S_FLAG_TXE, SET, deadline_ms))
    {
        return false;
    }

    GPIO_ResetBits(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN);
    SPI_I2S_SendData(SPI1, word);
    complete = SpiWait(SPI_I2S_FLAG_TXE, SET, deadline_ms) &&
               SpiWait(SPI_I2S_FLAG_BSY, RESET, deadline_ms);
    GPIO_SetBits(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN);
    /* 全双工接收端未接线，先读 DR，再读 SR，清除 RXNE 和可能残留的 OVR。 */
    (void)SPI_I2S_ReceiveData(SPI1);
    (void)SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_OVR);
    return complete;
}

bool DDS_Init(void)
{
    GPIO_InitTypeDef gpio_init;
    SPI_InitTypeDef spi_init;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);
    SPI_I2S_DeInit(SPI1);
    GPIO_SetBits(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN);
    gpio_init.GPIO_Pin = RLC_DDS_SELECT_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(RLC_DDS_GPIO, &gpio_init);

    gpio_init.GPIO_Pin = RLC_DDS_CLOCK_PIN | RLC_DDS_DATA_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(RLC_DDS_GPIO, &gpio_init);
#if RLC_AD9833_USE_MCO
    gpio_init.GPIO_Pin = RLC_DDS_MCO_PIN;
    GPIO_Init(RLC_DDS_GPIO, &gpio_init);
    RCC_MCOConfig(RCC_MCO_HSE);
#endif
    /* 16 位、MSB 优先、时钟空闲高、首个下降沿采样、72 MHz 除 32。 */
    spi_init.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    spi_init.SPI_Mode = SPI_Mode_Master;
    spi_init.SPI_DataSize = SPI_DataSize_16b;
    spi_init.SPI_CPOL = SPI_CPOL_High;
    spi_init.SPI_CPHA = SPI_CPHA_1Edge;
    spi_init.SPI_NSS = SPI_NSS_Soft;
    spi_init.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_32;
    spi_init.SPI_FirstBit = SPI_FirstBit_MSB;
    spi_init.SPI_CRCPolynomial = 7u;
    SPI_Init(SPI1, &spi_init);
    SPI_Cmd(SPI1, ENABLE);
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
