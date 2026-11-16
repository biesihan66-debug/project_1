/*
 * 开短路校准和补偿。RlcCalibration 由调用方分配，记录频率和反馈档组合的复阻抗。
 * open_ohm[f][r]、short_ohm[f][r] 的 f 是频率索引，r 是反馈档索引。
 * open_mask、short_mask、ideal_open_mask 存在同一个结构体，位序与两张阻抗表一致。
 * corrected 由自动测量模块换算成 OLED 第 2 行的元件值。
 * 本模块只更新调用方提供的 RAM 校准表，持久化由板级应用处理。
 */
#include "open_short_calibration.h"
#include "rlc_impedance_calculation.h"

#include <float.h>
#include <math.h>

#if RLC_FREQUENCY_COUNT * RLC_RANGE_COUNT > 32u
#error "Calibration setting masks require at most 32 combinations."
#endif

/* 用 0 表示索引越界。有效组合总是返回单独的一位。 */
static uint32_t setting_mask(uint8_t frequency_index, uint8_t range_index)
{
    if (frequency_index >= RLC_FREQUENCY_COUNT || range_index >= RLC_RANGE_COUNT) {
        return 0u;
    }
    return UINT32_C(1) << (frequency_index * RLC_RANGE_COUNT + range_index);
}

static bool same_impedance(RlcComplex a, RlcComplex b)
{
    return a.re == b.re && a.im == b.im;
}

bool rlc_calibration_set(RlcCalibration *calibration, uint8_t frequency_index,
                         uint8_t range_index, bool open, RlcComplex raw_ohm)
{
    const uint32_t mask = setting_mask(frequency_index, range_index);
    if (calibration == NULL || mask == 0u || !rlc_complex_is_finite(raw_ohm)) {
        return false;
    }

    if (open) {
        if ((raw_ohm.re == 0.0f && raw_ohm.im == 0.0f) ||
            ((calibration->short_mask & mask) != 0u &&
             same_impedance(raw_ohm, calibration->short_ohm[frequency_index][range_index]))) {
            return false;
        }
        calibration->open_ohm[frequency_index][range_index] = raw_ohm;
        calibration->open_mask |= mask;
        calibration->ideal_open_mask &= ~mask;
    } else {
        const uint32_t measured_open = calibration->open_mask & ~calibration->ideal_open_mask;
        if ((measured_open & mask) != 0u &&
            same_impedance(raw_ohm, calibration->open_ohm[frequency_index][range_index])) {
            return false;
        }
        calibration->short_ohm[frequency_index][range_index] = raw_ohm;
        calibration->short_mask |= mask;
    }
    return true;
}

bool rlc_calibration_set_ideal_open(RlcCalibration *calibration, uint8_t frequency_index,
                                    uint8_t range_index)
{
    const uint32_t mask = setting_mask(frequency_index, range_index);
    if (calibration == NULL || mask == 0u) {
        return false;
    }

    const RlcComplex zero = {0.0f, 0.0f};
    calibration->open_ohm[frequency_index][range_index] = zero;
    calibration->open_mask |= mask;
    calibration->ideal_open_mask |= mask;
    return true;
}

bool rlc_calibration_has_pair(const RlcCalibration *calibration, uint8_t frequency_index,
                              uint8_t range_index)
{
    const uint32_t mask = setting_mask(frequency_index, range_index);
    if (calibration == NULL || (calibration->open_mask & calibration->short_mask & mask) == 0u) {
        return false;
    }

    const RlcComplex short_ohm = calibration->short_ohm[frequency_index][range_index];
    if (!rlc_complex_is_finite(short_ohm)) {
        return false;
    }
    if ((calibration->ideal_open_mask & mask) != 0u) {
        return true;
    }
    const RlcComplex open_ohm = calibration->open_ohm[frequency_index][range_index];
    return rlc_complex_is_finite(open_ohm) &&
           (open_ohm.re != 0.0f || open_ohm.im != 0.0f) &&
           !same_impedance(open_ohm, short_ohm);
}

bool rlc_calibration_apply(const RlcCalibration *calibration, uint8_t frequency_index,
                            uint8_t range_index, RlcComplex raw_ohm, RlcComplex *corrected)
{
    if (corrected == NULL || !rlc_complex_is_finite(raw_ohm) ||
        !rlc_calibration_has_pair(calibration, frequency_index, range_index)) {
        return false;
    }

    const RlcComplex short_ohm = calibration->short_ohm[frequency_index][range_index];
    const RlcComplex raw_minus_short = {raw_ohm.re - short_ohm.re, raw_ohm.im - short_ohm.im};
    if (!rlc_complex_is_finite(raw_minus_short)) {
        return false;
    }
    if ((calibration->ideal_open_mask & setting_mask(frequency_index, range_index)) != 0u) {
        *corrected = raw_minus_short;
        return true;
    }

    /* 等效模型先消除串联短路阻抗，再扣除开路并联导纳。
       化简为 Zm / (1 - Zm/Zo)，使 Zm 为零时也能直接得到零。 */
    const RlcComplex open_ohm = calibration->open_ohm[frequency_index][range_index];
    const RlcComplex open_minus_short = {open_ohm.re - short_ohm.re, open_ohm.im - short_ohm.im};
    RlcComplex ratio;
    if (!rlc_complex_divide(raw_minus_short, open_minus_short, &ratio)) {
        return false;
    }
    const RlcComplex denominator = {1.0f - ratio.re, -ratio.im};
    if (fmaxf(fabsf(denominator.re), fabsf(denominator.im)) <= 16.0f * FLT_EPSILON) {
        return false;
    }
    return rlc_complex_divide(raw_minus_short, denominator, corrected);
}
