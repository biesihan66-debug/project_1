/*
 * 电桥串口诊断接口。输入当前 RlcMeasurementResult 和从测量命令到刷屏的毫秒数。
 * 结果通过板级 UART 发送，便于核对 OLED 第 1 至 4 行和两路采样的幅值、相位。
 * OLED 显示完成后调用，避免串口输出占用单次测量的显示预算。
 */
#ifndef MEASUREMENT_DEBUG_OUTPUT_H
#define MEASUREMENT_DEBUG_OUTPUT_H

#include "rlc_measurement_types.h"

void measurement_debug_output_result(const RlcMeasurementResult *result,
                                     uint32_t display_elapsed_ms, bool display_ok);
void measurement_debug_output_calibration(bool open, RlcStatus status);

#endif
