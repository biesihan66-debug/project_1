/*
 * 开短路补偿接口。自动测量模块按 [频率索引][反馈档索引] 读取补偿。
 * open_ohm 和 short_ohm 为夹具在同一设置下的原始复阻抗，单位 Ω。
 * 掩码第 frequency_index * RLC_RANGE_COUNT + range_index 位对应一个设置。
 * ideal_open_mask 标记测不到开路导纳的设置，仍要求有有效的短路记录。
 * 校准表初始应清零；App_RLC.c 持有正在使用和待提交的两张表，保存成功才切换。
 * 补偿结果用于 OLED 第 2 行数值；测量结果页的 CAL/RAW 标志显示在第 4 行。
 */
#ifndef RLC_CALIBRATION_H
#define RLC_CALIBRATION_H

#include "RLC_Types.h"

typedef struct
{
    RLC_Complex open_ohm[RLC_FREQUENCY_COUNT][RLC_RANGE_COUNT];  /* 开路复阻抗，Ω。 */
    RLC_Complex short_ohm[RLC_FREQUENCY_COUNT][RLC_RANGE_COUNT]; /* 短路残余阻抗，Ω。 */
    uint32_t open_mask;       /* 每一位标记对应设置是否有开路记录。 */
    uint32_t short_mask;      /* 每一位标记对应设置是否有短路记录。 */
    uint32_t ideal_open_mask; /* 开路电流低于分辨率时，用零导纳近似的设置。 */
} RLC_Calibration;

/* 写入一组采集值：两个 index 是表下标；open=true 为开路，false 为短路。
 * raw_ohm 填测得的复阻抗。有效则写入并返回 true，无效返回 false。 */
bool Calibration_Set(RLC_Calibration *calibration, uint8_t frequency_index, uint8_t range_index,
                     bool open, RLC_Complex raw_ohm);

/* 激励有效且开路电流低于测量分辨率时，登记零并联导纳。 */
bool Calibration_SetIdealOpen(RLC_Calibration *calibration, uint8_t frequency_index,
                              uint8_t range_index);

/* 指定频率和反馈档是否同时有可用的开路、短路记录；有则返回 true。 */
bool Calibration_HasPair(const RLC_Calibration *calibration, uint8_t frequency_index,
                         uint8_t range_index);

/* raw_ohm 输入待补偿阻抗，corrected 指向接收结果的变量，单位均为 Ω。
 * 成功返回 true；缺少校准或补偿分母接近零时返回 false，corrected 保持原值。 */
bool Calibration_Apply(const RLC_Calibration *calibration, uint8_t frequency_index,
                       uint8_t range_index, RLC_Complex raw_ohm, RLC_Complex *corrected);

#endif
