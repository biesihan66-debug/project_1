/*
 * STM32 板级公开接口。pin/ADC/定时器映射由 BSP_PinMap.h 管理。
 * 本头文件只公开板级基础功能；DDS、ADC、Flash、OLED 通信各有自己的同名头文件。
 * 板级计时用于去抖和测量超时。这里不安排 OLED 行号，行号由 OLED_Display.c 定义。
 */
#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include "RLC_Hardware.h"

/* 配置时钟、GPIO、DDS、ADC、串口和显示总线；成功 true，失败 false。 */
bool BSP_Init(void);

/* 返回已经绑定好四项硬件操作和反馈参数的只读表，交给 RLC_Measure... 使用。 */
const RLC_Hardware *BSP_GetMeasureHardware(void);

/* 获取毫秒计数；约 49.7 天回绕。用无符号时间差计算时长。 */
uint32_t BSP_GetMillis(void);

/* 当前时刻是否仍早于 deadline_ms；截止时间由 BSP_GetMillis()+允许耗时 得到。 */
bool BSP_BeforeDeadline(uint32_t deadline_ms);

/* 只读 PB10 原始电平：按下为 true；去抖和长短按判断由 Key.c 负责。 */
bool BSP_KeyIsPressed(void);

/* 从 USART1 发送以 '\0' 结束的文字；OLED 刷新后再打印诊断信息。 */
void BSP_DebugWrite(const char *text);

#endif
