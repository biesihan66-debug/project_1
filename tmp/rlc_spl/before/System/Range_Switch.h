/*
 * 反馈档控制接口。三根译码线在 BSP_PinMap.h；电阻/电容表在 BSP_Board.c。
 * range_index 为 0~4 的档位下标，不是电阻值。所选反馈阻值显示在 OLED 第 4 行。
 */
#ifndef RANGE_SWITCH_H
#define RANGE_SWITCH_H

#include <stdint.h>

/* 配置控制引脚，并置于安全档。 */
void Range_Init(void);
/* 写入档位；调用前停止 DDS，切换后的稳定等待由 BSP_Board.c 统一处理。 */
void Range_Set(uint8_t range_index);

#endif
