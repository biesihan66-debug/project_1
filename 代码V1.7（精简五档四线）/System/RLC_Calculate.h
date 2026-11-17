/*
 * RLC 测量计算接口。采样模块生成 RLC_Frame，自动测量模块调用本接口。
 * frame.packed_samples[i] 低半字是被测件电压，高半字是跨阻输出。
 * RLC_Phasors.dut_v、tia_v 保存峰值相量，单位 V。
 * RLC_Phasors.dut_peak_codes、tia_peak_codes 保存峰值幅度，单位 ADC 码。
 * component 和 component_value 最终分别进入 OLED 第 1、2 行。复阻抗与相量用于串口诊断。
 * 函数返回 false 时不写入输出参数。
 */
#ifndef RLC_CALCULATE_H
#define RLC_CALCULATE_H

#include "RLC_Types.h"

/* frame 指向采样帧；out 指向接收两路相量的变量，函数成功后才读取它。
 * 例如 RLC_Demodulate(&frame, &phasors)。内部拟合基波并去直流，过载由调用方判断。 */
bool RLC_Demodulate(const RLC_Frame *frame, RLC_Phasors *out);

/* phasors 输入两路电压相量；feedback_ohm 为当前反馈复阻抗（Ω）。
 * out 接收被测复阻抗，计算 Z = -Vdut * Zf / Vtia；成功 true，失败 false。 */
bool RLC_CalculateImpedance(RLC_Phasors phasors, RLC_Complex feedback_ohm, RLC_Complex *out);

/* 根据阻抗判断 R/L/C；无法确定时返回 RLC_COMPONENT_UNKNOWN。
 * 相对门槛按 |Re(Z)| 缩放，绝对门槛单位 Ω，通常由测量模块固定传入。 */
RLC_Component RLC_Classify(RLC_Complex impedance, float relative_phase_floor,
                           float absolute_reactance_floor_ohm);

/* impedance 输入复阻抗，frequency_hz 是测量频率，kind 是已识别的 R/L/C 类型。
 * value_out 接收串联等效元件值（Ω/H/F）；成功 true，失败 false。 */
bool RLC_GetComponentValue(RLC_Complex impedance, uint32_t frequency_hz, RLC_Component kind,
                           float *value_out);

/* 校准模块共用的有限值检查与有保护的复数除法。 */
bool RLC_ComplexIsFinite(RLC_Complex value);
bool RLC_ComplexDivide(RLC_Complex numerator, RLC_Complex denominator, RLC_Complex *out);

#endif
