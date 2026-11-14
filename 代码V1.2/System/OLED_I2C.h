/*
 * OLED 的底层 I2C 通信。PB6/PB7 与地址在 BSP_PinMap.h，通常由 OLED_Display.c 调用。
 * 本模块只发送字节，不计算显示行号；第 1~4 行布局由 OLED_Display.c 决定。
 */
#ifndef OLED_I2C_H
#define OLED_I2C_H

#include <stdbool.h>
#include <stdint.h>

/* 初始化总线，并尝试解除 I2C BUSY 状态。 */
void OLED_I2C_Init(void);
/* control=0x00 发命令，0x40 发像素；data 为数组，count 为字节数。
 * 返回 true 表示发送成功；false 表示无效数据或通信失败。 */
bool OLED_I2C_Write(uint8_t control, const uint8_t *data, uint16_t count);

#endif
