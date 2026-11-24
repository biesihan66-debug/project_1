/*
 * 测量模块对外只提供下面三个函数，由 App_RLC.c 调用。
 * App_Init() 成功后即可调用；本模块直接调用 BSP 设置、采样、计算和停止激励。
 * 参数约定：calibration 是开短路校准表；mode 选择基础或扩展模式。
 * 返回的 RLC_Result 是一份完整结果：status 判断成功与否，component_value 存元件值。
 * OLED 第 1/2/3/4 行依次显示类型、数值、频率、反馈档或错误。
 * 示例：result = RLC_MeasureAuto(&active_calibration, RLC_MODE_EXTENDED);
 */
#ifndef RLC_MEASURE_H
#define RLC_MEASURE_H

#include "RLC_Calibration.h"

/* The UI must confirm the physical JP1 position before selecting a profile. */
void RLC_SelectSenseProfile(bool sense_four_wire);

typedef enum
{
    RLC_MODE_BASIC,   /* 100 Hz、1 kHz、10 kHz */
    RLC_MODE_EXTENDED /* 加入 100 kHz */
} RLC_Mode;

/* 自动测量：mode 选基础/扩展；calibration 可为 NULL（不补偿）。
 * 只有返回结果的 status == RLC_STATUS_OK 时，才读取 component 和 component_value。
 * 函数返回前已关闭激励；无须调用额外的停止函数。 */
RLC_Result RLC_MeasureAuto(const RLC_Calibration *calibration, RLC_Mode mode);
/* 固定设置测量：frequency_index 和 range_index 都是表下标，不直接填写 Hz/Ω。
 * 例如 1 kHz、1 kΩ 档填 RLC_FREQUENCY_1_KHZ、RLC_RANGE_1_KOHM；返回规则同上。 */
RLC_Result RLC_MeasureFixed(const RLC_Calibration *calibration, uint8_t frequency_index,
                            uint8_t range_index);
/* 采集校准：open=true 时夹具开路，false 时短接；calibration 接收本次数据。
 * 基础模式采三频，扩展模式采四频；返回 OK 才更新传入的表，失败保留原表。
 * 本函数不写 Flash；应用等开路和短路均成功后，再统一保存。 */
RLC_Status RLC_CaptureCalibration(RLC_Calibration *calibration, bool open, RLC_Mode mode);

#endif
