/*
 * 反馈量程控制。range_index 是按 10 Ω 至 100 kΩ 升序排列的零基档位。
 * range_output_code[] 将档位映射到 BSP_PinMap.h 的 PB0/PB1/PB11。
 * 硬件需在复位及切换期间保持稳定的反馈路径。当前档位进入 OLED 第 4 行。
 */
#include "BSP_PinMap.h"
#include "Range_Switch.h"
#include "BSP_Board.h"

/* 下标是软件档位，值是外部译码器输入。5 至 7 由硬件接成安全状态。 */
static const uint8_t range_output_code[RLC_RANGE_COUNT] = {0u, 1u, 2u, 3u, 4u};

void Range_Set(uint8_t range_index)
{
    uint32_t all_pins =
        (1u << RLC_RANGE_BIT0_PIN) | (1u << RLC_RANGE_BIT1_PIN) | (1u << RLC_RANGE_BIT2_PIN);
    uint32_t code = range_output_code[range_index];
    uint32_t set_pins = ((code & 1u) << RLC_RANGE_BIT0_PIN) |
                        (((code >> 1) & 1u) << RLC_RANGE_BIT1_PIN) |
                        (((code >> 2) & 1u) << RLC_RANGE_BIT2_PIN);

    /* 同一次 BSRR 写入更新全部选择位，减少 GPIO 顺序引入的中间档。 */
    STM32_REG32(RLC_RANGE_GPIO + 0x10u) = ((all_pins & ~set_pins) << 16) | set_pins;
}

void Range_Init(void)
{
    Range_Set(RLC_RANGE_SAFE_INDEX);
    BSP_GPIO_Config(RLC_RANGE_GPIO, RLC_RANGE_BIT0_PIN, STM32_GPIO_OUT_PP);
    BSP_GPIO_Config(RLC_RANGE_GPIO, RLC_RANGE_BIT1_PIN, STM32_GPIO_OUT_PP);
    BSP_GPIO_Config(RLC_RANGE_GPIO, RLC_RANGE_BIT2_PIN, STM32_GPIO_OUT_PP);
}
