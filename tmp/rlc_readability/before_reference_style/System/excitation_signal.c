/*
 * AD9833 正弦源控制。SPI1 的接线和 MCLK 来自 board_pin_map.h。
 * excitation_hz 为实际调谐字换算的 Hz，由采样模块读取以计算每点相位增量。
 * 屏幕第 3 行显示选择的标称测试频率，TP1 处用示波器核对频率和电平。
 */
#include "board_pin_map.h"
#include "rlc_board_internal.h"

#define SPI_CR1 STM32_REG32(STM32_SPI1 + 0x00u)
#define SPI_SR  STM32_REG32(STM32_SPI1 + 0x08u)
#define SPI_DR  STM32_REG16(STM32_SPI1 + 0x0Cu)

#define SPI_TXE              (1u << 1)
#define SPI_BSY              (1u << 7)
#define AD9833_B28           0x2000u
#define AD9833_RESET         0x0100u
#define AD9833_FREQUENCY_0   0x4000u
#define AD9833_PHASE_0       0xC000u
#define AD9833_DATA_MASK     0x3FFFu
#define AD9833_ACCUMULATOR   (1u << 28)

/* 只有成功发送完整调谐字并解除复位后，才供采样使用。 */
static float excitation_hz;

static bool spi_wait(uint32_t mask, bool set, uint32_t deadline_ms)
{
    while (((SPI_SR & mask) != 0u) != set) {
        if (!rlc_board_before_deadline(deadline_ms)) {
            return false;
        }
    }
    return true;
}

static bool write_dds_word(uint16_t word, uint32_t deadline_ms)
{
    bool complete;

    if (!rlc_board_before_deadline(deadline_ms)
        || !spi_wait(SPI_TXE, true, deadline_ms)) {
        return false;
    }

    stm32_gpio_set(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN, false);
    SPI_DR = word;
    complete = spi_wait(SPI_TXE, true, deadline_ms)
            && spi_wait(SPI_BSY, false, deadline_ms);
    stm32_gpio_set(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN, true);
    (void)SPI_DR; /* 全双工 SPI 的接收内容未连接，每帧读出清 RXNE。 */
    (void)SPI_SR; /* 清除可能残留的 OVR。 */
    return complete;
}

bool rlc_board_excitation_initialize(void)
{
    STM32_RCC_APB2ENR |= 1u << 12;
    stm32_gpio_set(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN, true);
    stm32_gpio_configure(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN, STM32_GPIO_OUT_PP);
    stm32_gpio_configure(RLC_DDS_GPIO, RLC_DDS_CLOCK_PIN, STM32_GPIO_AF_PP);
    stm32_gpio_configure(RLC_DDS_GPIO, RLC_DDS_DATA_PIN, STM32_GPIO_AF_PP);
#if RLC_AD9833_USE_MCO
    stm32_gpio_configure(RLC_DDS_GPIO, RLC_DDS_MCO_PIN, STM32_GPIO_AF_PP);
    STM32_RCC_CFGR = (STM32_RCC_CFGR & ~(7u << 24)) | (6u << 24);
#endif
    /* 16 位、MSB 优先、时钟空闲高、首个下降沿采样、72 MHz 除 32。 */
    SPI_CR1 = (1u << 11) | (1u << 9) | (1u << 8) | (4u << 3) | (1u << 2) | (1u << 1);
    SPI_CR1 |= 1u << 6;
    return rlc_board_excitation_reset(rlc_board_millis() + 5u);
}

bool rlc_board_excitation_reset(uint32_t deadline_ms)
{
    excitation_hz = 0.0f;
    return write_dds_word(AD9833_B28 | AD9833_RESET, deadline_ms);
}

bool rlc_board_excitation_start(uint32_t nominal_hz, uint32_t deadline_ms)
{
    uint32_t tuning_word = (uint32_t)(((uint64_t)nominal_hz * AD9833_ACCUMULATOR
                            + RLC_AD9833_MCLK_HZ / 2u) / RLC_AD9833_MCLK_HZ);

    if (!write_dds_word((uint16_t)(AD9833_FREQUENCY_0 | (tuning_word & AD9833_DATA_MASK)),
                        deadline_ms)
        || !write_dds_word((uint16_t)(AD9833_FREQUENCY_0 | (tuning_word >> 14)), deadline_ms)
        || !write_dds_word(AD9833_PHASE_0, deadline_ms)
        || !write_dds_word(AD9833_B28, deadline_ms)) {
        return false;
    }
    excitation_hz = (float)tuning_word
                  * ((float)RLC_AD9833_MCLK_HZ / (float)AD9833_ACCUMULATOR);
    return true;
}

float rlc_board_excitation_actual_hz(void)
{
    return excitation_hz;
}
