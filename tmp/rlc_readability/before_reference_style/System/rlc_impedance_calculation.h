/*
 * RLC 测量计算接口。采样模块生成 RlcSampleFrame，自动测量模块调用本接口。
 * frame.packed_samples[i] 低半字是被测件电压，高半字是跨阻输出。
 * RlcPhasorPair.dut_v、tia_v 保存峰值相量，单位 V。
 * RlcPhasorPair.dut_peak_codes、tia_peak_codes 保存峰值幅度，单位 ADC 码。
 * component 和 value_si 最终分别进入 OLED 第 1、2 行。复阻抗与相量用于串口诊断。
 * 函数返回 false 时不写入输出参数。
 */
#ifndef RLC_IMPEDANCE_CALCULATION_H
#define RLC_IMPEDANCE_CALCULATION_H

#include "rlc_measurement_types.h"

/* 对两路样本做带直流项的正弦拟合。过载标志由调用方单独处理。 */
bool rlc_demodulate_frame(const RlcSampleFrame *frame, RlcPhasorPair *out);

/* 根据跨阻电路的反相关系求被测件复阻抗，feedback_ohm 单位 Ω。 */
bool rlc_calculate_impedance(RlcPhasorPair phasors, RlcComplex feedback_ohm,
                             RlcComplex *out);

/* 相对门槛按 |Re(Z)| 缩放，绝对门槛的单位为 Ω。 */
RlcComponent rlc_classify_impedance(RlcComplex z, float relative_phase_floor,
                                   float absolute_reactance_floor_ohm);

/* 从复阻抗求串联等效值，输出单位为 Ω、H 或 F。 */
bool rlc_component_value(RlcComplex z, uint32_t frequency_hz, RlcComponent kind,
                         float *out_si);

/* 校准模块共用的有限值检查与有保护的复数除法。 */
bool rlc_complex_is_finite(RlcComplex value);
bool rlc_complex_divide(RlcComplex numerator, RlcComplex denominator, RlcComplex *out);

#endif
