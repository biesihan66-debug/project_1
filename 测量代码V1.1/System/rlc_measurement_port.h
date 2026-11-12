/*
 * 自动测量与板级驱动的接口。context 由板级或主机替身持有。
 * feedback_ohm[range]、feedback_capacitance_f[range] 按反馈档索引。
 * 所有 deadline_ms 使用 millis() 的同一计时基准，允许 32 位回绕。
 * range_index 进入 OLED 第 4 行，采集的电压和跨阻数据供测量及串口诊断。
 */
#ifndef RLC_MEASUREMENT_PORT_H
#define RLC_MEASUREMENT_PORT_H

#include "rlc_measurement_types.h"

typedef struct {
    void *context;
    uint32_t (*millis)(void *context);
    RlcStatus (*select_setting)(void *context, uint8_t frequency_index,
                                uint8_t range_index, uint32_t deadline_ms);
    RlcStatus (*capture)(void *context, uint8_t cycles, uint32_t deadline_ms,
                         RlcSampleFrame *frame);
    void (*stop)(void *context);
    float feedback_ohm[RLC_RANGE_COUNT];
    float feedback_capacitance_f[RLC_RANGE_COUNT];
    float short_stimulus_min_codes; /* 短接时仍可用于估算残余阻抗的最小激励幅度。 */
} RlcMeasurementPort;

#endif
