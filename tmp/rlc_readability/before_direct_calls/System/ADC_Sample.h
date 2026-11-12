/*
 * 双 ADC + DMA 采样接口：ADC1 读 DUT 电压，ADC2 读 TIA 输出，两路同步采样。
 * 缓冲数组 packed_adc_samples[] 位于 ADC_Sample.c，采样帧仅引用它，不复制数组。
 * 数据经 RLC_Calculate.c 换算后显示；过载等错误最终进入 OLED 第 4 行。
 */
#ifndef ADC_SAMPLE_H
#define ADC_SAMPLE_H

#include "RLC_Types.h"

/* 初始化采样硬件并校准两路 ADC；成功 true，失败 false。 */
bool ADC_Init(void);
/* nominal_hz 为标称 Hz，actual_hz 为 DDS 实际 Hz，cycles 为采集周期数。
 * deadline_ms 为绝对截止时间；frame 指向接收采样说明的变量。
 * 返回 OK 后才可读 frame；样本只在下一次采集前有效。 */
RLC_Status ADC_Capture(uint32_t nominal_hz, float actual_hz, uint8_t cycles, uint32_t deadline_ms,
                       RLC_Frame *frame);

#endif
