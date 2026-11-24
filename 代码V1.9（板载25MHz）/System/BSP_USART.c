/*
 * USART1 调试输出。PA9（J2-19）发送，115200 波特、8-N-1。
 * text 由调用方提供，deadline_ms 是本次写入的绝对截止时刻。
 * App_RLC.c 在 OLED 刷新后调用串口诊断，本模块不写 OLED。
 */
#include "BSP_USART.h"
#include "BSP_PinMap.h"
#include "BSP_Board.h"
#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_rcc.h"
#include "stm32h7xx_ll_usart.h"

/* BSP_USART_Init() 启用 USART1 后置位，初始化失败时避免访问串口。 */
static bool uart_ready;

void BSP_USART_Init(void)
{
    LL_GPIO_InitTypeDef gpio = {0};

    LL_RCC_SetUSARTClockSource(LL_RCC_USART16_CLKSOURCE_PCLK2);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_USART1);
    gpio.Pin = RLC_DEBUG_TX_PIN;
    gpio.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio.Speed = LL_GPIO_SPEED_FREQ_LOW;
    gpio.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio.Alternate = LL_GPIO_AF_7;
    LL_GPIO_Init(RLC_DEBUG_GPIO, &gpio);

    LL_USART_ConfigCharacter(USART1, LL_USART_DATAWIDTH_8B, LL_USART_PARITY_NONE,
                            LL_USART_STOPBITS_1);
    LL_USART_SetTransferDirection(USART1, LL_USART_DIRECTION_TX);
    LL_USART_SetBaudRate(USART1, RLC_APB_CLOCK_HZ, LL_USART_PRESCALER_DIV1,
                         LL_USART_OVERSAMPLING_16, RLC_DEBUG_BAUD);
    LL_USART_Enable(USART1);
    uart_ready = true;
}

void BSP_DebugWrite(const char *text)
{
    uint32_t deadline_ms;

    if (text == NULL || !uart_ready)
    {
        return;
    }
    deadline_ms = BSP_GetMillis() + 100u;
    while (*text != '\0')
    {
        while (LL_USART_IsActiveFlag_TXE_TXFNF(USART1) == 0u)
        {
            if (!BSP_BeforeDeadline(deadline_ms))
            {
                return;
            }
        }
        LL_USART_TransmitData8(USART1, (uint8_t)*text++);
    }
}
