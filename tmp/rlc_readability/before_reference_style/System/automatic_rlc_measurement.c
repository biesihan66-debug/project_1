/*
 * 一键测量调度。入口为 rlc_measure_auto()、rlc_measure_fixed() 和
 * rlc_capture_calibration()；main.c 传入板级端口和开短路校准记录。
 * 自动测量顺序：1 kHz 试测 -> 选择适合的频率/反馈档 -> 正式复测 -> 停止激励。
 *
 * 变量位置与含义：
 * - 本文件 rlc_frequency_hz[]：频率表，单位 Hz；下标 0/1/2/3 为四个频点。
 * - RlcTrial.result：函数内的试测结果；frequency_index 是它对应的频率下标。
 * - result.range_index：反馈档下标，访问 port->feedback_ohm[]（Ω）与
 *   feedback_capacitance_f[]（F），0 为最低增益档；具体参数在板级模块。
 * - visited[]：当前函数已试过的频率或反馈档，true 表示跳过，避免来回切换。
 * - deadline_ms：SysTick 绝对截止时刻；dut/tia_peak_codes 为 ADC 峰值码数。
 * 开路记录按频率、档位采集，短路残余按频率在最低增益档测量后供各档共用。
 * 最终 result.component、value_si、frequency_hz、range_index 依次送至 OLED
 * 第 1、2、3、4 行。异常状态覆盖第 4 行，两路幅相和耗时由串口输出。
 */
#include "automatic_rlc_measurement.h"
#include "rlc_impedance_calculation.h"

#include <math.h>

const uint32_t rlc_frequency_hz[RLC_FREQUENCY_COUNT] = {100u, 1000u, 10000u, 100000u};

/* 给显示刷新保留时间。短窗试测和正式测量有各自的绝对截止时刻。 */
enum {
    SCOUT_CYCLES = 4,
    FINAL_CYCLES = 16,
    CALIBRATION_CYCLES = 24,
    SCOUT_LIMIT_MS = 650,
    MEASUREMENT_LIMIT_MS = 900,
    CALIBRATION_LIMIT_MS = 8000
};

static const float dut_min_codes = 30.0f;
static const float tia_min_codes = 7.0f;
static const float tia_preferred_min_codes = 110.0f;
static const float tia_preferred_max_codes = 1350.0f;
static const float tia_target_codes = 600.0f;
static const float relative_phase_floor = 0.015f;
static const float absolute_reactance_floor_ohm = 0.020f;
static const float two_pi = 6.28318530717958648f;

typedef struct {
    RlcMeasurementResult result;
    uint8_t frequency_index;
} RlcTrial;

static bool port_ready(const RlcMeasurementPort *port)
{
    return port != NULL && port->millis != NULL && port->select_setting != NULL &&
           port->capture != NULL && port->stop != NULL;
}

static bool time_expired(const RlcMeasurementPort *port, uint32_t deadline_ms)
{
    return port->millis(port->context) - deadline_ms < UINT32_C(0x80000000);
}

/* Rf 与 Cf 并联。板级表填各档有效反馈值，I/Q 算法据此反推被测电流。 */
static RlcComplex feedback_impedance(const RlcMeasurementPort *port,
                                     uint8_t frequency_index, uint8_t range_index)
{
    const float rf = port->feedback_ohm[range_index];
    const float cf = port->feedback_capacitance_f[range_index];
    const float pole = two_pi * (float)rlc_frequency_hz[frequency_index] * rf * cf;
    const float re = rf / (1.0f + pole * pole);
    return (RlcComplex){re, -re * pole};
}

static RlcTrial capture_impedance(const RlcMeasurementPort *port,
                                 uint8_t frequency_index, uint8_t range_index,
                                 uint8_t cycles, float minimum_dut_codes,
                                 uint32_t deadline_ms)
{
    RlcTrial trial = {.result.status = RLC_STATUS_HARDWARE_ERROR};
    RlcMeasurementResult *result = &trial.result;
    RlcSampleFrame frame = {0};
    const RlcComplex zf = feedback_impedance(port, frequency_index, range_index);

    trial.frequency_index = frequency_index;
    result->frequency_hz = rlc_frequency_hz[frequency_index];
    result->range_index = range_index;
    if (!isfinite(zf.re) || zf.re <= 0.0f || !isfinite(zf.im)) {
        return trial;
    }
    if (time_expired(port, deadline_ms)) {
        result->status = RLC_STATUS_TIMEOUT;
        return trial;
    }

    result->status = port->select_setting(port->context, frequency_index, range_index, deadline_ms);
    if (result->status != RLC_STATUS_OK) {
        return trial;
    }
    result->status = port->capture(port->context, cycles, deadline_ms, &frame);
    if (result->status != RLC_STATUS_OK) {
        return trial;
    }
    if (time_expired(port, deadline_ms)) {
        result->status = RLC_STATUS_TIMEOUT;
    } else if (frame.dut_clipped || frame.tia_clipped) {
        result->status = RLC_STATUS_OVERLOAD;
    } else if (!rlc_demodulate_frame(&frame, &result->phasors)) {
        result->status = RLC_STATUS_HARDWARE_ERROR;
    } else if (result->phasors.dut_peak_codes < minimum_dut_codes ||
               result->phasors.tia_peak_codes < tia_min_codes) {
        result->status = RLC_STATUS_LOW_SIGNAL;
    } else if (!rlc_calculate_impedance(result->phasors, zf, &result->impedance_ohm)) {
        result->status = RLC_STATUS_LOW_SIGNAL;
    }
    return trial;
}

static bool fatal_status(RlcStatus status)
{
    return status == RLC_STATUS_TIMEOUT || status == RLC_STATUS_HARDWARE_ERROR ||
           status == RLC_STATUS_CALIBRATION_ERROR;
}

/* 用相邻档作为边界，再以试测幅度预测更接近目标幅度的档。 */
static uint8_t next_range(const RlcMeasurementPort *port, const RlcTrial *trial)
{
    const uint8_t current = trial->result.range_index;
    const float peak = trial->result.phasors.tia_peak_codes;
    uint8_t choice = current;

    if (trial->result.status == RLC_STATUS_OVERLOAD) {
        return current == 0u ? current : (uint8_t)(current - 1u);
    }
    if (peak < tia_min_codes) {
        return current + 1u < RLC_RANGE_COUNT ? (uint8_t)(current + 1u) : current;
    }
    if (peak >= tia_preferred_min_codes && peak <= tia_preferred_max_codes) {
        return current;
    }

    const RlcComplex zf = feedback_impedance(port, trial->frequency_index, current);
    const float old_gain = hypotf(zf.re, zf.im);
    float best_distance = fabsf(peak - tia_target_codes);
    for (uint8_t candidate = 0u; candidate < RLC_RANGE_COUNT; ++candidate) {
        const RlcComplex zf_next = feedback_impedance(port, trial->frequency_index, candidate);
        const float predicted = peak * hypotf(zf_next.re, zf_next.im) / old_gain;
        const float distance = fabsf(predicted - tia_target_codes);
        if (predicted >= tia_min_codes && predicted <= tia_preferred_max_codes &&
            distance < best_distance) {
            choice = candidate;
            best_distance = distance;
        }
    }
    return choice;
}

static RlcTrial scout_frequency(const RlcMeasurementPort *port, uint8_t frequency_index,
                                uint32_t deadline_ms)
{
    RlcTrial best = {.result.status = RLC_STATUS_LOW_SIGNAL};
    uint8_t range_index = 0u;
    bool visited[RLC_RANGE_COUNT] = {false};

    for (uint8_t attempt = 0u; attempt < RLC_RANGE_COUNT; ++attempt) {
        RlcTrial trial = capture_impedance(port, frequency_index, range_index, SCOUT_CYCLES,
                                            dut_min_codes, deadline_ms);
        const RlcStatus status = trial.result.status;
        if (fatal_status(status)) {
            /* 试测时间用完时，保留已有读数，让后面的正式复测确认。 */
            if (status == RLC_STATUS_TIMEOUT && best.result.status == RLC_STATUS_OK) {
                return best;
            }
            return trial;
        }
        if (status == RLC_STATUS_OK || best.result.status != RLC_STATUS_OK) {
            best = trial;
        }
        if (status == RLC_STATUS_LOW_SIGNAL && trial.result.phasors.dut_peak_codes < dut_min_codes) {
            break;
        }
        visited[range_index] = true;
        range_index = next_range(port, &trial);
        if (visited[range_index]) {
            break;
        }
    }
    return best;
}

/* 对频率和档位一致的校准记录做补偿。输出只在状态 OK 时携带有效元件值。 */
static void resolve_component(RlcTrial *trial, const RlcCalibration *calibration)
{
    RlcMeasurementResult *result = &trial->result;
    if (result->status != RLC_STATUS_OK) {
        return;
    }
    if (calibration != NULL && rlc_calibration_has_pair(calibration, trial->frequency_index,
                                                        result->range_index)) {
        if (!rlc_calibration_apply(calibration, trial->frequency_index, result->range_index,
                                   result->impedance_ohm, &result->impedance_ohm)) {
            result->status = RLC_STATUS_CALIBRATION_ERROR;
            return;
        }
        result->calibrated = true;
    }
    result->component = rlc_classify_impedance(result->impedance_ohm, relative_phase_floor,
                                                absolute_reactance_floor_ohm);
    if (!rlc_component_value(result->impedance_ohm, result->frequency_hz,
                             result->component, &result->value_si)) {
        result->status = RLC_STATUS_AMBIGUOUS;
    }
}

/* 扩展范围内优先用 100 kHz。大电容、高阻和大电感使用较低频率。 */
static uint8_t preferred_frequency(const RlcTrial *trial, RlcMeasurementMode mode)
{
    const float value = trial->result.value_si;
    const bool extended = mode == RLC_MODE_EXTENDED;

    switch (trial->result.component) {
    case RLC_COMPONENT_R:
        return value >= 50000.0f ? RLC_FREQUENCY_100_HZ : RLC_FREQUENCY_1_KHZ;
    case RLC_COMPONENT_L:
        if (extended && value <= 1.1e-3f) {
            return RLC_FREQUENCY_100_KHZ;
        }
        if (value <= 2.0e-3f) {
            return RLC_FREQUENCY_10_KHZ;
        }
        return value >= 50.0e-3f ? RLC_FREQUENCY_100_HZ : RLC_FREQUENCY_1_KHZ;
    case RLC_COMPONENT_C:
        if (extended && value <= 110.0e-9f) {
            return RLC_FREQUENCY_100_KHZ;
        }
        if (value <= 110.0e-9f) {
            return RLC_FREQUENCY_10_KHZ;
        }
        return value >= 1.0e-6f ? RLC_FREQUENCY_100_HZ : RLC_FREQUENCY_1_KHZ;
    default:
        return trial->frequency_index;
    }
}

/* R 的确认频点用来发现低 Q 电感。只有第二频点的电抗折回后
 * 与首测一致，才把首测的 R 改判为 L 或 C。 */
static bool same_component_trend(const RlcTrial *first, const RlcTrial *second)
{
    const RlcMeasurementResult *initial = &first->result;
    const RlcMeasurementResult *confirmation = &second->result;
    if (initial->component == confirmation->component) {
        return true;
    }
    if (initial->component != RLC_COMPONENT_R ||
        (confirmation->component != RLC_COMPONENT_L && confirmation->component != RLC_COMPONENT_C)) {
        return false;
    }
    const float frequency_ratio = (float)initial->frequency_hz / (float)confirmation->frequency_hz;
    float predicted_reactance = confirmation->impedance_ohm.im;
    if (confirmation->component == RLC_COMPONENT_L) {
        predicted_reactance *= frequency_ratio;       /* 电感电抗与频率成正比。 */
    } else {
        predicted_reactance *= 1.0f / frequency_ratio; /* 电容电抗与频率成反比。 */
    }
    const float tolerance = fmaxf(absolute_reactance_floor_ohm,
                                  fabsf(initial->impedance_ohm.re) * 0.02f);
    return fabsf(predicted_reactance - initial->impedance_ohm.im) <= tolerance &&
           fabsf(confirmation->impedance_ohm.im) >= fabsf(confirmation->impedance_ohm.re) * 0.05f;
}

static RlcTrial scout_and_resolve(const RlcMeasurementPort *port,
                                 const RlcCalibration *calibration,
                                 uint8_t frequency_index, uint32_t deadline_ms)
{
    RlcTrial trial = scout_frequency(port, frequency_index, deadline_ms);
    resolve_component(&trial, calibration);
    return trial;
}

/* 已在优选频率测过 R 后，是否还需排查被误认成电阻的低 Q 元件？
 * 低于 50 kΩ 才检查 10 kHz；不超过 200 Ω 时额外检查 100 kHz。 */
static bool resistance_needs_recheck(const RlcMeasurementResult *result, uint8_t frequency_index)
{
    if (result->component != RLC_COMPONENT_R || result->value_si >= 50000.0f) {
        return false;
    }
    if (frequency_index == RLC_FREQUENCY_10_KHZ) {
        return true;
    }
    return frequency_index == RLC_FREQUENCY_100_KHZ && result->value_si <= 200.0f;
}

/* 新读数替换旧读数的规则：先要有效，再要类型变化合理，最后看是否更合适。
 * 两次都失败时，仅保留识别/校准错误，便于显示比“信号弱”更具体的原因。 */
static bool candidate_is_better(const RlcTrial *best, const RlcTrial *candidate,
                                RlcMeasurementMode mode)
{
    if (candidate->result.status != RLC_STATUS_OK) {
        return best->result.status != RLC_STATUS_OK &&
               (candidate->result.status == RLC_STATUS_AMBIGUOUS ||
                candidate->result.status == RLC_STATUS_CALIBRATION_ERROR);
    }
    if (best->result.status != RLC_STATUS_OK) {
        return true;
    }
    if (!same_component_trend(best, candidate)) {
        return false;
    }
    return candidate->result.component != best->result.component ||
           candidate->frequency_index == preferred_frequency(best, mode);
}

/* 从 1 kHz 起测。各频点最多试一次，已有可用读数时保留给正式复测。 */
static RlcTrial choose_frequency_and_range(const RlcMeasurementPort *port,
                                           const RlcCalibration *calibration,
                                           RlcMeasurementMode mode, uint32_t deadline_ms)
{
    static const uint8_t alternate_order[] = {
        RLC_FREQUENCY_10_KHZ, RLC_FREQUENCY_100_KHZ, RLC_FREQUENCY_100_HZ
    };
    RlcTrial best = scout_and_resolve(port, calibration, RLC_FREQUENCY_1_KHZ, deadline_ms);
    bool visited[RLC_FREQUENCY_COUNT] = {false};
    visited[RLC_FREQUENCY_1_KHZ] = true;

    for (size_t i = 0u; i < sizeof alternate_order / sizeof alternate_order[0]; ++i) {
        if (fatal_status(best.result.status) || time_expired(port, deadline_ms)) {
            break;
        }
        uint8_t frequency_index = alternate_order[i];
        if (best.result.status == RLC_STATUS_OK) {
            const uint8_t preferred = preferred_frequency(&best, mode);
            if (!visited[preferred]) {
                frequency_index = preferred;
            } else if (!resistance_needs_recheck(&best.result, frequency_index)) {
                continue;
            }
        }
        if (visited[frequency_index] ||
            (mode == RLC_MODE_BASIC && frequency_index == RLC_FREQUENCY_100_KHZ)) {
            continue;
        }

        visited[frequency_index] = true;
        RlcTrial candidate = scout_and_resolve(port, calibration, frequency_index, deadline_ms);
        if (candidate.result.status == RLC_STATUS_HARDWARE_ERROR) {
            return candidate;
        }
        if (candidate_is_better(&best, &candidate, mode)) {
            best = candidate;
        }
    }
    return best;
}

static RlcMeasurementResult finish_measurement(const RlcMeasurementPort *port,
                                                RlcTrial trial, uint32_t start_ms)
{
    port->stop(port->context);
    trial.result.elapsed_ms = port->millis(port->context) - start_ms;
    if (trial.result.elapsed_ms >= MEASUREMENT_LIMIT_MS &&
        trial.result.status == RLC_STATUS_OK) {
        trial.result.status = RLC_STATUS_TIMEOUT;
    }
    return trial.result;
}

RlcMeasurementResult rlc_measure_auto(const RlcMeasurementPort *port,
                                      const RlcCalibration *calibration,
                                      RlcMeasurementMode mode)
{
    if (!port_ready(port) || (mode != RLC_MODE_BASIC && mode != RLC_MODE_EXTENDED)) {
        return (RlcMeasurementResult){.status = RLC_STATUS_HARDWARE_ERROR};
    }
    const uint32_t start_ms = port->millis(port->context);
    RlcTrial trial = choose_frequency_and_range(port, calibration, mode, start_ms + SCOUT_LIMIT_MS);
    if (trial.result.status == RLC_STATUS_OK) {
        const RlcComponent expected = trial.result.component;
        trial = capture_impedance(port, trial.frequency_index, trial.result.range_index,
                                  FINAL_CYCLES, dut_min_codes, start_ms + MEASUREMENT_LIMIT_MS);
        resolve_component(&trial, calibration);
        if (trial.result.status == RLC_STATUS_OK && trial.result.component != expected) {
            trial.result.status = RLC_STATUS_AMBIGUOUS;
        }
    }
    return finish_measurement(port, trial, start_ms);
}

RlcMeasurementResult rlc_measure_fixed(const RlcMeasurementPort *port,
                                       const RlcCalibration *calibration,
                                       uint8_t frequency_index, uint8_t range_index)
{
    if (!port_ready(port) || frequency_index >= RLC_FREQUENCY_COUNT || range_index >= RLC_RANGE_COUNT) {
        return (RlcMeasurementResult){.status = RLC_STATUS_HARDWARE_ERROR};
    }
    const uint32_t start_ms = port->millis(port->context);
    RlcTrial trial = capture_impedance(port, frequency_index, range_index, FINAL_CYCLES,
                                      dut_min_codes, start_ms + MEASUREMENT_LIMIT_MS);
    resolve_component(&trial, calibration);
    return finish_measurement(port, trial, start_ms);
}

/* 开路电流低于底噪时以零导纳记录，但必须看到有效激励且无削顶。 */
static RlcStatus capture_open(const RlcMeasurementPort *port, RlcCalibration *record,
                              uint8_t frequency_index, uint8_t range_index, uint32_t deadline_ms)
{
    const RlcTrial trial = capture_impedance(port, frequency_index, range_index,
                                             CALIBRATION_CYCLES, dut_min_codes, deadline_ms);
    const RlcMeasurementResult *result = &trial.result;
    if (result->status == RLC_STATUS_LOW_SIGNAL && result->phasors.dut_peak_codes >= dut_min_codes &&
        result->phasors.tia_peak_codes < tia_min_codes) {
        return rlc_calibration_set_ideal_open(record, frequency_index, range_index)
                   ? RLC_STATUS_OK : RLC_STATUS_CALIBRATION_ERROR;
    }
    if (result->status != RLC_STATUS_OK) {
        return result->status;
    }
    return rlc_calibration_set(record, frequency_index, range_index, true, result->impedance_ohm)
               ? RLC_STATUS_OK : RLC_STATUS_CALIBRATION_ERROR;
}

static RlcStatus capture_short(const RlcMeasurementPort *port, RlcCalibration *record,
                               uint8_t frequency_index, uint32_t deadline_ms)
{
    const RlcTrial trial = capture_impedance(port, frequency_index, 0u,
                                             CALIBRATION_CYCLES, port->short_stimulus_min_codes,
                                             deadline_ms);
    if (trial.result.status != RLC_STATUS_OK) {
        return trial.result.status;
    }
    /* 短接残余若超出低阻档的合理范围，提示重新插紧短路片。 */
    const RlcComplex z = trial.result.impedance_ohm;
    if (hypotf(z.re, z.im) > 1.0f) {
        return RLC_STATUS_CALIBRATION_ERROR;
    }
    for (uint8_t range = 0u; range < RLC_RANGE_COUNT; ++range) {
        if (!rlc_calibration_set(record, frequency_index, range, false, z)) {
            return RLC_STATUS_CALIBRATION_ERROR;
        }
    }
    return RLC_STATUS_OK;
}

RlcStatus rlc_capture_calibration(const RlcMeasurementPort *port,
                                  RlcCalibration *calibration, bool open,
                                  RlcMeasurementMode mode)
{
    if (!port_ready(port) || calibration == NULL ||
        (mode != RLC_MODE_BASIC && mode != RLC_MODE_EXTENDED)) {
        return RLC_STATUS_HARDWARE_ERROR;
    }
    const uint8_t frequency_count = mode == RLC_MODE_BASIC
                                        ? RLC_FREQUENCY_100_KHZ : RLC_FREQUENCY_COUNT;
    const uint32_t deadline_ms = port->millis(port->context) + CALIBRATION_LIMIT_MS;
    RlcCalibration candidate = *calibration;
    RlcStatus status = RLC_STATUS_OK;
    for (uint8_t frequency = 0u; frequency < frequency_count && status == RLC_STATUS_OK;
         ++frequency) {
        if (open) {
            for (uint8_t range = 0u; range < RLC_RANGE_COUNT && status == RLC_STATUS_OK; ++range) {
                status = capture_open(port, &candidate, frequency, range, deadline_ms);
            }
        } else {
            status = capture_short(port, &candidate, frequency, deadline_ms);
        }
    }
    port->stop(port->context);
    if (status == RLC_STATUS_OK) {
        *calibration = candidate;
    }
    return status;
}
