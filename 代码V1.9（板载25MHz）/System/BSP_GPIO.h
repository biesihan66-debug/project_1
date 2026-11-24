/*
 * 由 BSP_Init() 启用 GPIOA/GPIOB/GPIOC，先设量程安全档，再配置按键。
 * 按键电平经 BSP_KeyIsPressed() 供 Key.c 判断，不直接写 OLED。
 */
#ifndef BSP_GPIO_H
#define BSP_GPIO_H

void BSP_GPIO_Init(void);

#endif
