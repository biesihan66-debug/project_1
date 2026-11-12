/*
 * 测量算法需要的四项硬件操作：读取时间、设置频率/档位、采集、停止。
 * BSP_Board.c 已将真实硬件函数填入这张表；一般只需修改反馈参数，不改函数绑定。
 * 这里的 (*millis) 是“指向函数的指针”：调用 port->millis(port->context)
 * 就是调用板级计时函数，并不是新的计时器。其余三个函数指针同理。
 * context 是随调用传回的上下文；实物板用 NULL，主机测试用它保存模拟硬件状态。
 * feedback_ohm[range]、feedback_capacitance_f[range] 按反馈档索引。
 * 所有 deadline_ms 使用 millis() 的同一计时基准，允许 32 位回绕。
 * range_index 进入 OLED 第 4 行，采集的电压和跨阻数据供测量及串口诊断。
 */
#ifndef RLC_HARDWARE_H
#define RLC_HARDWARE_H

#include "RLC_Types.h"

typedef struct
{
    void *context;                     /* 调用时原样传给四个函数，测量模块不解读它。 */
    uint32_t (*millis)(void *context); /* 返回上电以来的毫秒数。 */
    /* 选择频率下标和反馈档下标，等待稳定；返回 OK 后才能采样。 */
    RLC_Status (*select_setting)(void *context, uint8_t frequency_index, uint8_t range_index,
                                 uint32_t deadline_ms);
    /* 采集 cycles 个激励周期，frame 接收采样数据说明；数据缓冲由 ADC 模块持有。 */
    RLC_Status (*capture)(void *context, uint8_t cycles, uint32_t deadline_ms, RLC_Frame *frame);
    void (*stop)(void *context);                   /* 关闭激励并回到安全反馈档。 */
    float feedback_ohm[RLC_RANGE_COUNT];           /* 各档有效反馈电阻，Ω。 */
    float feedback_capacitance_f[RLC_RANGE_COUNT]; /* 各档并联反馈电容，F。 */
    float short_stimulus_min_codes;                /* 短接时仍可用于估算残余阻抗的最小激励幅度。 */
} RLC_Hardware;

#endif
