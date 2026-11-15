/*
 * 板级 GPIO 入口。PB10 内部上拉，按下为低电平。
 * gpio_init 只在 BSP_GPIO_Init() 内使用；量程输出由 Range_Switch.c 初始化。
 * BSP_KeyIsPressed() 由 Key.c 读取，测量和校准页面由 App_RLC.c 更新。
 */
#include "BSP_GPIO.h"
#include "stm32f10x.h"
#include "BSP_PinMap.h"
#include "BSP_Board.h"
#include "Range_Switch.h"

void BSP_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio_init;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO,
                           ENABLE);
    Range_Init();

    GPIO_SetBits(RLC_BUTTON_GPIO, RLC_BUTTON_PIN);
    gpio_init.GPIO_Pin = RLC_BUTTON_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(RLC_BUTTON_GPIO, &gpio_init);
}

bool BSP_KeyIsPressed(void)
{
    return GPIO_ReadInputDataBit(RLC_BUTTON_GPIO, RLC_BUTTON_PIN) == Bit_RESET;
}
