/*
 * USART1 调试口初始化，由 BSP_Init() 在 GPIO 就绪后调用。
 * BSP_DebugWrite() 的声明位于 BSP_Board.h，串口数据不进入 OLED。
 */
#ifndef BSP_USART_H
#define BSP_USART_H

void BSP_USART_Init(void);

#endif
