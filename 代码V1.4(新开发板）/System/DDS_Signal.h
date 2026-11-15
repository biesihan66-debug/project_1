/*
 * AD9833 正弦源接口。接线与 MCLK 参数在 BSP_PinMap.h，实际频率保存在 DDS_Signal.c。
 * deadline_ms 为允许等待到的绝对毫秒时刻；以下 bool 返回 true 表示 SPI 写入成功。
 * OLED 第 3 行显示标称频率，采样解调使用实际频率。
 */
#ifndef DDS_SIGNAL_H
#define DDS_SIGNAL_H

#include <stdbool.h>
#include <stdint.h>

/* 初始化 SPI 和 DDS，保持输出关闭。 */
bool DDS_Init(void);
/* 停止激励；到达截止时间仍不能发送复位字时返回 false。 */
bool DDS_Stop(uint32_t deadline_ms);
/* nominal_hz 填目标频率（Hz），写入调谐字后开启正弦输出。 */
bool DDS_SetFrequency(uint32_t nominal_hz, uint32_t deadline_ms);
/* 返回调谐字对应的实际 Hz；输出关闭时为 0。 */
float DDS_GetActualFrequencyHz(void);

#endif
