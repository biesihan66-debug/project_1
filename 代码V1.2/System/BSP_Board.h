/*
 * STM32 板级公开接口。pin/ADC/定时器映射由 BSP_PinMap.h 管理。
 * 本头文件只公开板级基础功能；DDS、ADC、Flash、OLED 通信各有自己的同名头文件。
 * 板级计时用于去抖和测量超时。这里不安排 OLED 行号，行号由 OLED_Display.c 定义。
 */
#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#include "RLC_Types.h"

/* 配置时钟、GPIO、DDS、ADC、串口和显示总线；成功 true，失败 false。 */
bool BSP_Init(void);

/* 量程参数实际定义在 BSP_Board.c；下标 0~4 对应五档，不是 GPIO 编号。
 * extern const 表示这里引用一份只读表；需要改实测参数时只改 .c 中的定义。 */
extern const float bsp_feedback_ohm[RLC_RANGE_COUNT];           /* 有效反馈电阻，Ω。 */
extern const float bsp_feedback_capacitance_f[RLC_RANGE_COUNT]; /* 并联反馈电容，F。 */
extern const float bsp_short_min_codes;                         /* 短路采集最低电压幅度，ADC 码。 */

/* frequency_index / range_index 填频率、反馈档下标，返回 OK 表示已切换并稳定。
 * deadline_ms 为允许等待到的绝对毫秒时刻；错误由返回值报告，收尾调用 BSP_StopMeasurement。 */
RLC_Status BSP_SelectMeasurement(uint8_t frequency_index, uint8_t range_index,
                                 uint32_t deadline_ms);

/* 在已选设置下采集 cycles 个周期；frame 接收采样说明，仅 OK 时有效。 */
RLC_Status BSP_CaptureFrame(uint8_t cycles, uint32_t deadline_ms, RLC_Frame *frame);

/* 停止激励并回到安全反馈档。 */
void BSP_StopMeasurement(void);

/* 获取毫秒计数；约 49.7 天回绕。用无符号时间差计算时长。 */
uint32_t BSP_GetMillis(void);

/* 当前时刻是否仍早于 deadline_ms；截止时间由 BSP_GetMillis()+允许耗时 得到。 */
bool BSP_BeforeDeadline(uint32_t deadline_ms);

/* 只读 PB10 原始电平：按下为 true；去抖和长短按判断由 Key.c 负责。 */
bool BSP_KeyIsPressed(void);

/* 从 USART1 发送以 '\0' 结束的文字；OLED 刷新后再打印诊断信息。 */
void BSP_DebugWrite(const char *text);

#endif
