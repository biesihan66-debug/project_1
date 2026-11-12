/*
 * RLC 相量解调和参数计算。自动测量模块传入采样帧和当前档位的反馈阻抗。
 * 相邻采样的相位差由 frame->cycles_per_sample 决定，frame->packed_samples[i]
 * 低 16 位是被测件电压 ADC 码，高 16 位是跨阻输出 ADC 码。
 * accumulate_frame() 在 FrameSums 中累计两路数据，共用正弦和余弦参考。
 * RlcPhasorPair 的峰值 ADC 码用于选档，峰值电压相量用于求复阻抗和串口诊断。
 * RlcMeasurementResult.component、value_si 由自动流程填入，显示到 OLED 第 1、2 行。
 */
#include "rlc_impedance_calculation.h"

#include <float.h>
#include <math.h>

#define RLC_TWO_PI 6.2831853071795864769f
#define RLC_ADC_MID_CODE 2048.0f
#define RLC_REFERENCE_NORMALIZATION_INTERVAL 32u

/* 单路采样相对于常数、cos、sin 的投影合计。 */
typedef struct {
    float total;
    float against_cos;
    float against_sin;
} ChannelSums;

/* 两路共用的正弦拟合矩阵，经消去常数项后只需解 2×2 方程。 */
typedef struct {
    float cos_total;
    float sin_total;
    float cos_squared;
    float sin_squared;
    float cos_times_sin;
    ChannelSums dut;
    ChannelSums tia;
} FrameSums;

static bool finite_float(float value)
{
    return value >= -FLT_MAX && value <= FLT_MAX;
}

bool rlc_complex_is_finite(RlcComplex value)
{
    return finite_float(value.re) && finite_float(value.im);
}

bool rlc_complex_divide(RlcComplex numerator, RlcComplex denominator, RlcComplex *out)
{
    if (out == NULL || !rlc_complex_is_finite(numerator) ||
        !rlc_complex_is_finite(denominator)) {
        return false;
    }

    /* 先缩放分母，避免直接对很小或很大的阻抗求模平方。 */
    const float scale = fmaxf(fabsf(denominator.re), fabsf(denominator.im));
    if (scale == 0.0f) {
        return false;
    }
    const float re = denominator.re / scale;
    const float im = denominator.im / scale;
    const float norm = re * re + im * im;
    const float a = numerator.re / scale;
    const float b = numerator.im / scale;
    const RlcComplex value = {
        (a * re + b * im) / norm,
        (b * re - a * im) / norm
    };

    if (!rlc_complex_is_finite(value)) {
        return false;
    }
    *out = value;
    return true;
}

static void accumulate_sample(ChannelSums *sums, float sample,
                              float cos_ref, float sin_ref)
{
    sums->total += sample;
    sums->against_cos += sample * cos_ref;
    sums->against_sin += sample * sin_ref;
}

static FrameSums accumulate_frame(const RlcSampleFrame *frame)
{
    const float phase_step = RLC_TWO_PI * frame->cycles_per_sample;
    const float cos_step = cosf(phase_step);
    const float sin_step = sinf(phase_step);
    float cos_ref = 1.0f;
    float sin_ref = 0.0f;
    FrameSums sums = {0};

    for (size_t index = 0u; index < frame->sample_count; ++index) {
        const uint32_t packed = frame->packed_samples[index];
        const float dut_code = (float)(uint16_t)packed - RLC_ADC_MID_CODE;
        const float tia_code = (float)(uint16_t)(packed >> 16) - RLC_ADC_MID_CODE;

        sums.cos_total += cos_ref;
        sums.sin_total += sin_ref;
        sums.cos_squared += cos_ref * cos_ref;
        sums.sin_squared += sin_ref * sin_ref;
        sums.cos_times_sin += cos_ref * sin_ref;
        accumulate_sample(&sums.dut, dut_code, cos_ref, sin_ref);
        accumulate_sample(&sums.tia, tia_code, cos_ref, sin_ref);

        const float next_cos = cos_ref * cos_step - sin_ref * sin_step;
        sin_ref = sin_ref * cos_step + cos_ref * sin_step;
        cos_ref = next_cos;
        if ((index + 1u) % RLC_REFERENCE_NORMALIZATION_INTERVAL == 0u) {
            /* 用一次近似倒平方根修正递推中的浮点模长漂移。 */
            const float correction = 1.5f - 0.5f * (cos_ref * cos_ref + sin_ref * sin_ref);
            cos_ref *= correction;
            sin_ref *= correction;
        }
    }
    return sums;
}

static RlcComplex solve_channel(ChannelSums channel, float mean_cos, float mean_sin,
                                float cc, float ss, float cs, float inv_determinant)
{
    const float rhs_cos = channel.against_cos - channel.total * mean_cos;
    const float rhs_sin = channel.against_sin - channel.total * mean_sin;

    /* 对 Re(A·exp(jωt))，sin 的实系数等于 -Im(A)。 */
    const RlcComplex codes = {
        (rhs_cos * ss - rhs_sin * cs) * inv_determinant,
        (rhs_cos * cs - rhs_sin * cc) * inv_determinant
    };
    return codes;
}

bool rlc_demodulate_frame(const RlcSampleFrame *frame, RlcPhasorPair *out)
{
    if (frame == NULL || out == NULL || frame->packed_samples == NULL ||
        frame->sample_count < 4u ||
        !(frame->cycles_per_sample > 0.0f && frame->cycles_per_sample < 0.5f) ||
        !(frame->dut_volts_per_code > 0.0f && finite_float(frame->dut_volts_per_code)) ||
        !(frame->tia_volts_per_code > 0.0f && finite_float(frame->tia_volts_per_code))) {
        return false;
    }

    const FrameSums sums = accumulate_frame(frame);
    const float inv_count = 1.0f / (float)frame->sample_count;
    const float mean_cos = sums.cos_total * inv_count;
    const float mean_sin = sums.sin_total * inv_count;
    const float cc = sums.cos_squared - sums.cos_total * mean_cos;
    const float ss = sums.sin_squared - sums.sin_total * mean_sin;
    const float cs = sums.cos_times_sin - sums.cos_total * mean_sin;
    const float determinant = cc * ss - cs * cs;
    const float trace = cc + ss;

    /* 拒绝相位跨度太小或两参考基近似共线的帧。 */
    if (!(cc > 0.0f && ss > 0.0f && determinant > 0.01f * trace * trace)) {
        return false;
    }
    const float inv_determinant = 1.0f / determinant;
    const RlcComplex dut = solve_channel(sums.dut, mean_cos, mean_sin,
                                         cc, ss, cs, inv_determinant);
    const RlcComplex tia = solve_channel(sums.tia, mean_cos, mean_sin,
                                         cc, ss, cs, inv_determinant);
    const RlcPhasorPair result = {
        {dut.re * frame->dut_volts_per_code, dut.im * frame->dut_volts_per_code},
        {tia.re * frame->tia_volts_per_code, tia.im * frame->tia_volts_per_code},
        sqrtf(dut.re * dut.re + dut.im * dut.im),
        sqrtf(tia.re * tia.re + tia.im * tia.im)
    };
    if (!rlc_complex_is_finite(result.dut_v) || !rlc_complex_is_finite(result.tia_v) ||
        !finite_float(result.dut_peak_codes) || !finite_float(result.tia_peak_codes)) {
        return false;
    }
    *out = result;
    return true;
}

bool rlc_calculate_impedance(RlcPhasorPair phasors, RlcComplex feedback_ohm,
                             RlcComplex *out)
{
    RlcComplex voltage_ratio;
    if (out == NULL || !rlc_complex_is_finite(feedback_ohm) ||
        (feedback_ohm.re == 0.0f && feedback_ohm.im == 0.0f) ||
        !rlc_complex_divide(phasors.dut_v, phasors.tia_v, &voltage_ratio)) {
        return false;
    }

    /* 跨阻输出反相，Z = -Zf × Vdut / Vtia。 */
    const RlcComplex impedance = {
        feedback_ohm.im * voltage_ratio.im - feedback_ohm.re * voltage_ratio.re,
        -feedback_ohm.re * voltage_ratio.im - feedback_ohm.im * voltage_ratio.re
    };
    if (!rlc_complex_is_finite(impedance)) {
        return false;
    }
    *out = impedance;
    return true;
}

RlcComponent rlc_classify_impedance(RlcComplex z, float relative_phase_floor,
                                   float absolute_reactance_floor_ohm)
{
    if (!rlc_complex_is_finite(z) ||
        !(relative_phase_floor >= 0.0f && finite_float(relative_phase_floor)) ||
        !(absolute_reactance_floor_ohm >= 0.0f && finite_float(absolute_reactance_floor_ohm))) {
        return RLC_COMPONENT_UNKNOWN;
    }

    const float floor_ohm = fmaxf(absolute_reactance_floor_ohm,
                                  relative_phase_floor * fabsf(z.re));
    if (z.im > floor_ohm) {
        return RLC_COMPONENT_L;
    }
    if (z.im < -floor_ohm) {
        return RLC_COMPONENT_C;
    }
    return z.re > 0.0f ? RLC_COMPONENT_R : RLC_COMPONENT_UNKNOWN;
}

bool rlc_component_value(RlcComplex z, uint32_t frequency_hz, RlcComponent kind,
                         float *out_si)
{
    float value;
    if (out_si == NULL || frequency_hz == 0u || !rlc_complex_is_finite(z)) {
        return false;
    }
    const float angular_frequency = RLC_TWO_PI * (float)frequency_hz;
    switch (kind) {
    case RLC_COMPONENT_R:
        value = z.re;
        break;
    case RLC_COMPONENT_L:
        value = z.im / angular_frequency;
        break;
    case RLC_COMPONENT_C:
        if (!(z.im < 0.0f)) {
            return false;
        }
        value = (-1.0f / z.im) / angular_frequency;
        break;
    default:
        return false;
    }
    if (!(value > 0.0f && finite_float(value))) {
        return false;
    }
    *out_si = value;
    return true;
}
