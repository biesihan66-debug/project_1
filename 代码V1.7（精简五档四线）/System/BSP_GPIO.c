/*
 * 板级 GPIO 入口。PB10（J3-12）内部上拉，按下为低电平。
 * gpio_init 只在 BSP_GPIO_Init() 内使用；量程输出由 Range_Switch.c 初始化。
 * BSP_KeyIsPressed() 由 Key.c 读取，测量和校准页面由 App_RLC.c 更新。
 */
#include "BSP_GPIO.h"
#include "BSP_Board.h"
#include "BSP_PinMap.h"
#include "Range_Switch.h"
#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_gpio.h"

void BSP_GPIO_Init(void)
{
    LL_GPIO_InitTypeDef gpio = {0};

    LL_AHB4_GRP1_EnableClock(LL_AHB4_GRP1_PERIPH_GPIOA | LL_AHB4_GRP1_PERIPH_GPIOB |
                            LL_AHB4_GRP1_PERIPH_GPIOC);
    Range_Init();

    gpio.Pin = RLC_BUTTON_PIN;
    gpio.Mode = LL_GPIO_MODE_INPUT;
    gpio.Pull = LL_GPIO_PULL_UP;
    LL_GPIO_Init(RLC_BUTTON_GPIO, &gpio);
}

bool BSP_KeyIsPressed(void)
{
    return LL_GPIO_IsInputPinSet(RLC_BUTTON_GPIO, RLC_BUTTON_PIN) == 0u;
}
