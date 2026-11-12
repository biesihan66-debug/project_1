/*
 * 开短路补偿接口。自动测量模块按 [频率索引][反馈档索引] 读取补偿。
 * open_ohm 和 short_ohm 为夹具在同一设置下的原始复阻抗，单位 Ω。
 * 掩码第 frequency_index * RLC_RANGE_COUNT + range_index 位对应一个设置。
 * ideal_open_mask 标记测不到开路导纳的设置，仍要求有有效的短路记录。
 * 校准表初始应清零。采集成功后再更新对应记录，读取方负责持久化。
 * 补偿结果用于 OLED 第 2 行数值；测量结果页的 CAL/RAW 标志显示在第 4 行。
 */
#ifndef OPEN_SHORT_CALIBRATION_H
#define OPEN_SHORT_CALIBRATION_H

#include "rlc_measurement_types.h"

typedef struct {
    RlcComplex open_ohm[RLC_FREQUENCY_COUNT][RLC_RANGE_COUNT];
    RlcComplex short_ohm[RLC_FREQUENCY_COUNT][RLC_RANGE_COUNT];
    uint32_t open_mask;
    uint32_t short_mask;
    uint32_t ideal_open_mask;
} RlcCalibration;

bool rlc_calibration_set(RlcCalibration *calibration, uint8_t frequency_index,
                         uint8_t range_index, bool open, RlcComplex raw_ohm);

/* 激励有效且开路电流低于测量分辨率时，登记零并联导纳。 */
bool rlc_calibration_set_ideal_open(RlcCalibration *calibration, uint8_t frequency_index,
                                    uint8_t range_index);

bool rlc_calibration_has_pair(const RlcCalibration *calibration, uint8_t frequency_index,
                              uint8_t range_index);

/* 缺少校准或补偿接近奇点时返回 false，corrected 保持原值。 */
bool rlc_calibration_apply(const RlcCalibration *calibration, uint8_t frequency_index,
                            uint8_t range_index, RlcComplex raw_ohm, RlcComplex *corrected);

#endif
