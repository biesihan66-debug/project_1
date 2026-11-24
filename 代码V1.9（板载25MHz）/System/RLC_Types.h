/*
 * RLC 电桥公共类型。采样模块交出 RLC_Frame，算法输出 RLC_Result。
 * 样本位置在 packed_samples[i]，低半字为电压通道，高半字为跨阻通道。
 * 频率和档位索引分别访问 rlc_frequency_hz[]、板级档位表及校准表。
 * result.component/status 对应 OLED 第 1/4 行，component_value 对应第 2 行，
 * frequency_hz 对应第 3 行，range_index 对应第 4 行。
 * typedef struct 将相关变量放在一起并取一个类型名；不是另外创建一份全局变量。
 * 阅读应用时重点看底部的 RLC_Result。DUT 指被测元件，TIA 指跨阻放大器。
 */
#ifndef RLC_TYPES_H
#define RLC_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RLC_FREQUENCY_COUNT   4u
#define RLC_RANGE_COUNT       5u
#define RLC_FREQUENCY_100_HZ  0u
#define RLC_FREQUENCY_1_KHZ   1u
#define RLC_FREQUENCY_10_KHZ  2u
#define RLC_FREQUENCY_100_KHZ 3u
#define RLC_RANGE_1_KOHM      2u

/* extern 表示这里只声明；数组实际定义在 RLC_Measure.c，值为 100/1000/10000/100000 Hz。 */
extern const uint32_t rlc_frequency_hz[RLC_FREQUENCY_COUNT];

typedef struct
{
    float real; /* 实部；单位由变量决定，例如阻抗为 Ω，相量为 V。 */
    float imag; /* 虚部；与实部使用同一单位。 */
} RLC_Complex;

typedef struct
{
    const uint32_t *packed_samples; /* 由板级 DMA 或主机测试提供，仅在下一次采集前有效。 */
    size_t sample_count;            /* packed_samples[] 中 DUT/TIA 两路电压采样对的数量。 */
    float cycles_per_sample;        /* 每个采样点前进的激励周期数，用于同步解调。 */
    float dut_volts_per_code;       /* 被测端电压通道的 V/ADC 码。 */
    float tia_volts_per_code;       /* 跨阻放大器输出通道的 V/ADC 码。 */
    const char *fault;              /* Capture failure detail for the serial log. */
    bool dut_clipped;               /* 被测端电压是否触及 ADC 上下限。 */
    bool tia_clipped;               /* 跨阻输出是否触及 ADC 上下限。 */
} RLC_Frame;

typedef struct
{
    RLC_Complex dut_v;    /* 被测端电压相量，V。 */
    RLC_Complex tia_v;    /* 跨阻输出相量，V；需结合反馈阻抗换算电流。 */
    float dut_peak_codes; /* 电压基波峰值，ADC 码；用于判断信号是否足够。 */
    float tia_peak_codes; /* 跨阻基波峰值，ADC 码；用于自动选择反馈档。 */
} RLC_Phasors;

typedef enum
{
    RLC_COMPONENT_UNKNOWN,
    RLC_COMPONENT_R,
    RLC_COMPONENT_L,
    RLC_COMPONENT_C
} RLC_Component;

typedef enum
{
    RLC_STATUS_OK,               /* 测量成功。 */
    RLC_STATUS_TIMEOUT,          /* 超过本次操作的截止时间。 */
    RLC_STATUS_OVERLOAD,         /* ADC 输入削顶，当前档无法测量。 */
    RLC_STATUS_LOW_SIGNAL,       /* 电压或电流信号太小。 */
    RLC_STATUS_AMBIGUOUS,        /* 暂时不能可靠判断 R/L/C。 */
    RLC_STATUS_HARDWARE_ERROR,   /* 硬件配置或采集失败。 */
    RLC_STATUS_CALIBRATION_ERROR /* 校准数据或补偿计算无效。 */
} RLC_Status;

/* 一次测量的结果。App_RLC.c 的 result 接收它，OLED_Display.c 读取它并显示。 */
typedef struct
{
    const char *fault;             /* Static diagnostic text on failure. */
    RLC_Status status;         /* OLED 第 1/4 行：成功或错误；仅 OK 时数值有效。 */
    RLC_Component component;   /* OLED 第 1 行：R、L 或 C。 */
    float component_value;     /* OLED 第 2 行：Ω、H 或 F；显示时自动换单位。 */
    uint32_t frequency_hz;     /* OLED 第 3 行：实际选择的标称频率，Hz。 */
    uint8_t range_index;       /* OLED 第 4 行：由此下标查板级反馈电阻表。 */
    bool calibrated;           /* OLED 第 4 行：CAL/RAW，是否实际应用开短路补偿。 */
    uint32_t elapsed_ms;       /* 串口：算法执行时间，ms，不含显示刷新。 */
    RLC_Complex impedance_ohm; /* 串口：复阻抗，Ω；成功校准后为补偿后的结果。 */
    RLC_Phasors phasors;       /* 串口：两路解调相量；保留校准前的采集结果。 */
} RLC_Result;

#endif
