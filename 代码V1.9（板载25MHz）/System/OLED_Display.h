/*
 * OLED 页面接口。输入测量结果和反馈阻值（Ω），逻辑行号从 1 开始。
 * 第 1 行类型或状态，第 2 行数值和单位，第 3 行频率，第 4 行档位或错误。
 * 文字和页地址在 OLED_Display.c 定义，I2C 事务由板级接口执行。
 * 返回 false 表示显示器未初始化或总线事务失败。
 */
#ifndef OLED_DISPLAY_H
#define OLED_DISPLAY_H

#include "RLC_Types.h"

/* 初始化 SSD1306 并清屏；先调用一次，成功后才显示页面。 */
bool OLED_DisplayInit(void);
/* 清除上一份结果并显示忙碌提示，例如 "MEASURING"。 */
bool OLED_ShowActivity(const char *message);
/* line1~line4 分别是四行文本；每行可容纳 21 个完整的 ASCII 字符。 */
bool OLED_ShowLines(const char *line1, const char *line2, const char *line3, const char *line4);
/* result 是测量返回的完整结果，feedback_ohm 填当前档的反馈电阻（Ω）。
 * 本函数处理单位和错误页，四行位置固定，不需要调用者自己排版。 */
bool OLED_ShowResult(const RLC_Result *result, float feedback_ohm);
/* 把 RLC 状态转换为屏幕提示文字，返回的字符串无需释放。 */
const char *OLED_GetStatusText(RLC_Status status);

#endif
