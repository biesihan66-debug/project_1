/*
 * RLC 电桥公共类型。采样模块交出 RlcSampleFrame，算法输出 RlcMeasurementResult。
 * 样本位置在 packed_samples[i]，低半字为电压通道，高半字为跨阻通道。
 * 频率和档位索引分别访问 rlc_frequency_hz[]、板级档位表及校准表。
 * result.component/status 对应 OLED 第 1/4 行，value_si 对应第 2 行，
 * frequency_hz 对应第 3 行，range_index 对应第 4 行。
 */
#ifndef RLC_MEASUREMENT_TYPES_H
#define RLC_MEASUREMENT_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RLC_FREQUENCY_COUNT 4u
#define RLC_RANGE_COUNT 5u
#define RLC_FREQUENCY_100_HZ 0u
#define RLC_FREQUENCY_1_KHZ 1u
#define RLC_FREQUENCY_10_KHZ 2u
#define RLC_FREQUENCY_100_KHZ 3u
#define RLC_RANGE_1_KOHM 2u

extern const uint32_t rlc_frequency_hz[RLC_FREQUENCY_COUNT];

typedef struct {
    float re; /* 实部；单位由变量决定，例如阻抗为 Ω，相量为 V。 */
    float im; /* 虚部；与实部使用同一单位。 */
} RlcComplex;

typedef struct {
    const uint32_t *packed_samples; /* 由板级 DMA 或主机测试提供，仅在下一次采集前有效。 */
    size_t sample_count;       /* packed_samples[] 中 DUT/TIA 两路电压采样对的数量。 */
    float cycles_per_sample;   /* 每个采样点前进的激励周期数，用于同步解调。 */
    float dut_volts_per_code;  /* 被测端电压通道的 V/ADC 码。 */
    float tia_volts_per_code;  /* 跨阻放大器输出通道的 V/ADC 码。 */
    bool dut_clipped;          /* 被测端电压是否触及 ADC 上下限。 */
    bool tia_clipped;          /* 跨阻输出是否触及 ADC 上下限。 */
} RlcSampleFrame;

typedef struct {
    RlcComplex dut_v;          /* 被测端电压相量，V。 */
    RlcComplex tia_v;          /* 跨阻输出相量，V；需结合反馈阻抗换算电流。 */
    float dut_peak_codes;     /* 电压基波峰值，ADC 码；用于判断信号是否足够。 */
    float tia_peak_codes;     /* 跨阻基波峰值，ADC 码；用于自动选择反馈档。 */
} RlcPhasorPair;

typedef enum {
    RLC_COMPONENT_UNKNOWN,
    RLC_COMPONENT_R,
    RLC_COMPONENT_L,
    RLC_COMPONENT_C
} RlcComponent;

typedef enum {
    RLC_STATUS_OK,
    RLC_STATUS_TIMEOUT,
    RLC_STATUS_OVERLOAD,
    RLC_STATUS_LOW_SIGNAL,
    RLC_STATUS_AMBIGUOUS,
    RLC_STATUS_HARDWARE_ERROR,
    RLC_STATUS_CALIBRATION_ERROR
} RlcStatus;

typedef struct {
    RlcStatus status;          /* OLED 第 1/4 行：成功或错误；仅 OK 时数值有效。 */
    RlcComponent component;    /* OLED 第 1 行：R、L 或 C。 */
    float value_si;            /* OLED 第 2 行：Ω、H 或 F；显示时自动换单位。 */
    uint32_t frequency_hz;    /* OLED 第 3 行：实际选择的标称频率，Hz。 */
    uint8_t range_index;       /* OLED 第 4 行：由此下标查板级反馈电阻表。 */
    bool calibrated;          /* OLED 第 4 行：CAL/RAW，是否实际应用开短路补偿。 */
    uint32_t elapsed_ms;      /* 串口：算法执行时间，ms，不含显示刷新。 */
    RlcComplex impedance_ohm; /* 串口：复阻抗，Ω；成功校准后为补偿后的结果。 */
    RlcPhasorPair phasors;     /* 串口：两路解调相量；保留校准前的采集结果。 */
} RlcMeasurementResult;

#endif
