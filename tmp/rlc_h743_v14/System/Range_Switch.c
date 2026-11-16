/*
 * 反馈量程控制。range_index 是按 10 Ω 至 100 kΩ 升序排列的零基档位。
 * range_output_code[] 将档位映射到 BSP_PinMap.h 的 PB0/PB1/PB11。
 * 硬件需在复位及切换期间保持稳定的反馈路径。当前档位进入 OLED 第 4 行。
 */
#include "Range_Switch.h"
#include "BSP_PinMap.h"
#include "RLC_Types.h"
#include "stm32h7xx_ll_gpio.h"

/* 下标是软件档位，值是外部译码器输入。5 至 7 由硬件接成安全状态。 */
static const uint8_t range_output_code[RLC_RANGE_COUNT] = {0u, 1u, 2u, 3u, 4u};

#define RANGE_SELECT_PINS (RLC_RANGE_BIT0_PIN | RLC_RANGE_BIT1_PIN | RLC_RANGE_BIT2_PIN)

void Range_Set(uint8_t range_index)
{
    const uint8_t code = range_output_code[range_index];
    uint32_t set_pins = 0u;

    if ((code & 1u) != 0u)
    {
        set_pins |= RLC_RANGE_BIT0_PIN;
    }
    if ((code & 2u) != 0u)
    {
        set_pins |= RLC_RANGE_BIT1_PIN;
    }
    if ((code & 4u) != 0u)
    {
        set_pins |= RLC_RANGE_BIT2_PIN;
    }
    /* 一次写 BSRR 同步更新三位，避免短暂选到中间档。 */
    RLC_RANGE_GPIO->BSRR = ((RANGE_SELECT_PINS & ~set_pins) << 16) | set_pins;
}

void Range_Init(void)
{
    LL_GPIO_InitTypeDef gpio = {0};

    Range_Set(RLC_RANGE_SAFE_INDEX);
    gpio.Pin = RANGE_SELECT_PINS;
    gpio.Mode = LL_GPIO_MODE_OUTPUT;
    gpio.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio.Speed = LL_GPIO_SPEED_FREQ_LOW;
    LL_GPIO_Init(RLC_RANGE_GPIO, &gpio);
}
