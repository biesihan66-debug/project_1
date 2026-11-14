/*
 * F103 板级系统时钟与 1 ms SysTick。由 BSP_Init() 在外设初始化前调用。
 * 返回 true 表示 72 MHz 时钟和计时可用；失败时应用停止初始化。
 * SysTick_Handler() 和 uptime_ms 在 BSP_Board.c。本模块不输出到 OLED。
 */
#ifndef BSP_CLOCK_H
#define BSP_CLOCK_H

#include <stdbool.h>

bool BSP_Clock_Init(void);

#endif
