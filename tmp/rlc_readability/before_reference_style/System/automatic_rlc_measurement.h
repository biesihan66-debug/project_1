/*
 * 一键测量入口。main.c 按键事件调用自动模式，台架模式调用固定设置。
 * 校准数据位于 RlcCalibration，由调用方加载和持久化。
 * 结果位置见 RlcMeasurementResult，OLED 第 1 行类型、第 2 行值、
 * 第 3 行频率、第 4 行反馈量程或错误。
 */
#ifndef AUTOMATIC_RLC_MEASUREMENT_H
#define AUTOMATIC_RLC_MEASUREMENT_H

#include "open_short_calibration.h"
#include "rlc_measurement_port.h"

typedef enum {
    RLC_MODE_BASIC,    /* 100 Hz、1 kHz、10 kHz */
    RLC_MODE_EXTENDED  /* 加入 100 kHz */
} RlcMeasurementMode;

RlcMeasurementResult rlc_measure_auto(const RlcMeasurementPort *port,
                                      const RlcCalibration *calibration,
                                      RlcMeasurementMode mode);
RlcMeasurementResult rlc_measure_fixed(const RlcMeasurementPort *port,
                                       const RlcCalibration *calibration,
                                       uint8_t frequency_index, uint8_t range_index);
/* 基础模式仅采集三档频率，扩展模式包括 100 kHz。 */
RlcStatus rlc_capture_calibration(const RlcMeasurementPort *port,
                                  RlcCalibration *calibration, bool open,
                                  RlcMeasurementMode mode);

#endif
