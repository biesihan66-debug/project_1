/*
 * OLED 页面接口。输入测量结果和反馈阻值（Ω），逻辑行号从 1 开始。
 * 第 1 行类型或状态，第 2 行数值和单位，第 3 行频率，第 4 行档位或错误。
 * 文字和页地址在 oled_result_display.c 定义，I2C 事务由板级接口执行。
 * 返回 false 表示显示器未初始化或总线事务失败。
 */
#ifndef OLED_RESULT_DISPLAY_H
#define OLED_RESULT_DISPLAY_H

#include "rlc_measurement_types.h"

bool oled_result_display_initialize(void);
bool oled_result_display_show_activity(const char *message);
bool oled_result_display_show_lines(const char *line1, const char *line2,
                                    const char *line3, const char *line4);
bool oled_result_display_show_measurement(const RlcMeasurementResult *result,
                                          float feedback_ohm);
const char *oled_result_display_status_text(RlcStatus status);

#endif
