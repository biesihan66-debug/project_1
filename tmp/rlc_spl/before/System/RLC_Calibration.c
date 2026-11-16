/*
 * 开短路校准和补偿。RLC_Calibration 由调用方分配，记录频率和反馈档组合的复阻抗。
 * open_ohm[f][r]、short_ohm[f][r] 的 f 是频率索引，r 是反馈档索引。
 * open_mask、short_mask、ideal_open_mask 存在同一个结构体，位序与两张阻抗表一致。
 * corrected 由自动测量模块换算成 OLED 第 2 行的元件值。
 * 本模块只更新调用方提供的 RAM 校准表，持久化由板级应用处理。
 */
#include "RLC_Calibration.h"
#include "RLC_Calculate.h"

#include <float.h>
#include <math.h>

#if RLC_FREQUENCY_COUNT * RLC_RANGE_COUNT > 32u
#error "Calibration setting masks require at most 32 combinations."
#endif

/* 用 0 表示索引越界。有效组合总是返回单独的一位。 */
static uint32_t SettingMask(uint8_t frequency_index, uint8_t range_index)
{
    if (frequency_index >= RLC_FREQUENCY_COUNT || range_index >= RLC_RANGE_COUNT)
    {
        return 0u;
    }
    return UINT32_C(1) << (frequency_index * RLC_RANGE_COUNT + range_index);
}

static bool SameImpedance(RLC_Complex a, RLC_Complex b)
{
    return a.real == b.real && a.imag == b.imag;
}

bool Calibration_Set(RLC_Calibration *calibration, uint8_t frequency_index, uint8_t range_index,
                     bool open, RLC_Complex raw_ohm)
{
    const uint32_t mask = SettingMask(frequency_index, range_index);
    if (calibration == NULL || mask == 0u || !RLC_ComplexIsFinite(raw_ohm))
    {
        return false;
    }

    if (open)
    {
        if ((raw_ohm.real == 0.0f && raw_ohm.imag == 0.0f) ||
            ((calibration->short_mask & mask) != 0u &&
             SameImpedance(raw_ohm, calibration->short_ohm[frequency_index][range_index])))
        {
            return false;
        }
        calibration->open_ohm[frequency_index][range_index] = raw_ohm;
        calibration->open_mask |= mask;
        calibration->ideal_open_mask &= ~mask;
    }
    else
    {
        const uint32_t measured_open = calibration->open_mask & ~calibration->ideal_open_mask;
        if ((measured_open & mask) != 0u &&
            SameImpedance(raw_ohm, calibration->open_ohm[frequency_index][range_index]))
        {
            return false;
        }
        calibration->short_ohm[frequency_index][range_index] = raw_ohm;
        calibration->short_mask |= mask;
    }
    return true;
}

bool Calibration_SetIdealOpen(RLC_Calibration *calibration, uint8_t frequency_index,
                              uint8_t range_index)
{
    const uint32_t mask = SettingMask(frequency_index, range_index);
    if (calibration == NULL || mask == 0u)
    {
        return false;
    }

    const RLC_Complex zero = {0.0f, 0.0f};
    calibration->open_ohm[frequency_index][range_index] = zero;
    calibration->open_mask |= mask;
    calibration->ideal_open_mask |= mask;
    return true;
}

bool Calibration_HasPair(const RLC_Calibration *calibration, uint8_t frequency_index,
                         uint8_t range_index)
{
    const uint32_t mask = SettingMask(frequency_index, range_index);
    if (calibration == NULL || (calibration->open_mask & calibration->short_mask & mask) == 0u)
    {
        return false;
    }

    const RLC_Complex short_ohm = calibration->short_ohm[frequency_index][range_index];
    if (!RLC_ComplexIsFinite(short_ohm))
    {
        return false;
    }
    if ((calibration->ideal_open_mask & mask) != 0u)
    {
        return true;
    }
    const RLC_Complex open_ohm = calibration->open_ohm[frequency_index][range_index];
    return RLC_ComplexIsFinite(open_ohm) && (open_ohm.real != 0.0f || open_ohm.imag != 0.0f) &&
           !SameImpedance(open_ohm, short_ohm);
}

bool Calibration_Apply(const RLC_Calibration *calibration, uint8_t frequency_index,
                       uint8_t range_index, RLC_Complex raw_ohm, RLC_Complex *corrected)
{
    if (corrected == NULL || !RLC_ComplexIsFinite(raw_ohm) ||
        !Calibration_HasPair(calibration, frequency_index, range_index))
    {
        return false;
    }

    const RLC_Complex short_ohm = calibration->short_ohm[frequency_index][range_index];
    const RLC_Complex raw_minus_short = {raw_ohm.real - short_ohm.real,
                                         raw_ohm.imag - short_ohm.imag};
    if (!RLC_ComplexIsFinite(raw_minus_short))
    {
        return false;
    }
    if ((calibration->ideal_open_mask & SettingMask(frequency_index, range_index)) != 0u)
    {
        *corrected = raw_minus_short;
        return true;
    }

    /* 等效模型先消除串联短路阻抗，再扣除开路并联导纳。
       化简为 Zm / (1 - Zm/Zo)，使 Zm 为零时也能直接得到零。 */
    const RLC_Complex open_ohm = calibration->open_ohm[frequency_index][range_index];
    const RLC_Complex open_minus_short = {open_ohm.real - short_ohm.real,
                                          open_ohm.imag - short_ohm.imag};
    RLC_Complex ratio;
    if (!RLC_ComplexDivide(raw_minus_short, open_minus_short, &ratio))
    {
        return false;
    }
    const RLC_Complex denominator = {1.0f - ratio.real, -ratio.imag};
    if (fmaxf(fabsf(denominator.real), fabsf(denominator.imag)) <= 16.0f * FLT_EPSILON)
    {
        return false;
    }
    return RLC_ComplexDivide(raw_minus_short, denominator, corrected);
}
