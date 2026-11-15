/*
 * USART1 调试输出。PA9 发送，PA10 保持浮空输入，115200 波特、8-N-1。
 * text 由调用方提供，deadline_ms 是本次写入的绝对截止时刻。
 * App_RLC.c 在 OLED 刷新后调用串口诊断，本模块不写 OLED。
 */
#include "BSP_USART.h"
#include "stm32f10x.h"
#include "BSP_PinMap.h"
#include "BSP_Board.h"

/* BSP_USART_Init() 启用 USART1 后置位，初始化失败时避免访问串口。 */
static bool uart_ready;

void BSP_USART_Init(void)
{
    GPIO_InitTypeDef gpio_init;
    USART_InitTypeDef usart_init;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
    USART_DeInit(USART1);
    gpio_init.GPIO_Pin = RLC_DEBUG_TX_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(RLC_DEBUG_GPIO, &gpio_init);

    gpio_init.GPIO_Pin = RLC_DEBUG_RX_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(RLC_DEBUG_GPIO, &gpio_init);

    usart_init.USART_BaudRate = RLC_DEBUG_BAUD;
    usart_init.USART_WordLength = USART_WordLength_8b;
    usart_init.USART_StopBits = USART_StopBits_1;
    usart_init.USART_Parity = USART_Parity_No;
    usart_init.USART_Mode = USART_Mode_Tx;
    usart_init.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART1, &usart_init);
    USART_Cmd(USART1, ENABLE);
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
        while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET)
        {
            if (!BSP_BeforeDeadline(deadline_ms))
            {
                return;
            }
        }
        USART_SendData(USART1, (uint8_t)*text++);
    }
}
