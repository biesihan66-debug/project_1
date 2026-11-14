/*
 * F103 系统时钟，由 BSP_Init() 在外设初始化前调用。
 * 返回 true 表示 72 MHz 时钟可用。随后由 delay.c 启用 1 ms SysTick。
 * 时钟配置失败时应用停止初始化。本模块不输出到 OLED。
 */
#ifndef BSP_CLOCK_H
#define BSP_CLOCK_H

#include <stdbool.h>

bool BSP_Clock_Init(void);

#endif
