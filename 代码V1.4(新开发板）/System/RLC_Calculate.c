/*
 * RLC 相量解调和参数计算。自动测量模块传入采样帧和当前档位的反馈阻抗。
 * 相邻采样的相位差由 frame->cycles_per_sample 决定，frame->packed_samples[i]
 * 低 16 位是被测件电压 ADC 码，高 16 位是跨阻输出 ADC 码。
 * AccumulateFrame() 在 FrameSums 中累计两路数据，共用正弦和余弦参考。
 * RLC_Phasors 的峰值 ADC 码用于选档，峰值电压相量用于求复阻抗和串口诊断。
 * RLC_Result.component、component_value 由自动流程填入，显示到 OLED 第 1、2 行。
 */
#include "RLC_Calculate.h"

#include <float.h>
#include <math.h>

#define RLC_TWO_PI                           6.2831853071795864769f
#define RLC_ADC_MID_CODE                     2048.0f
#define RLC_REFERENCE_NORMALIZATION_INTERVAL 32u

/* 单路采样相对于常数、cos、sin 的投影合计。 */
typedef struct
{
    float total;
    float against_cos;
    float against_sin;
} ChannelSums;

/* 两路共用的正弦拟合矩阵，经消去常数项后只需解 2×2 方程。 */
typedef struct
{
    float cos_total;
    float sin_total;
    float cos_squared;
    float sin_squared;
    float cos_times_sin;
    ChannelSums dut;
    ChannelSums tia;
} FrameSums;

static bool FiniteFloat(float value)
{
    return value >= -FLT_MAX && value <= FLT_MAX;
}

bool RLC_ComplexIsFinite(RLC_Complex value)
{
    return FiniteFloat(value.real) && FiniteFloat(value.imag);
}

bool RLC_ComplexDivide(RLC_Complex numerator, RLC_Complex denominator, RLC_Complex *out)
{
    if (out == NULL || !RLC_ComplexIsFinite(numerator) || !RLC_ComplexIsFinite(denominator))
    {
        return false;
    }

    /* 先缩放分母，避免直接对很小或很大的阻抗求模平方。 */
    const float scale = fmaxf(fabsf(denominator.real), fabsf(denominator.imag));
    if (scale == 0.0f)
    {
        return false;
    }
    const float real = denominator.real / scale;
    const float imag = denominator.imag / scale;
    const float norm = real * real + imag * imag;
    const float numerator_real = numerator.real / scale;
    const float numerator_imag = numerator.imag / scale;
    const RLC_Complex value = {(numerator_real * real + numerator_imag * imag) / norm,
                               (numerator_imag * real - numerator_real * imag) / norm};

    if (!RLC_ComplexIsFinite(value))
    {
        return false;
    }
    *out = value;
    return true;
}

static void AccumulateSample(ChannelSums *sums, float sample, float cos_ref, float sin_ref)
{
    sums->total += sample;
    sums->against_cos += sample * cos_ref;
    sums->against_sin += sample * sin_ref;
}

static FrameSums AccumulateFrame(const RLC_Frame *frame)
{
    const float phase_step = RLC_TWO_PI * frame->cycles_per_sample;
    const float cos_step = cosf(phase_step);
    const float sin_step = sinf(phase_step);
    float cos_ref = 1.0f;
    float sin_ref = 0.0f;
    FrameSums sums = {0};

    for (size_t index = 0u; index < frame->sample_count; ++index)
    {
        const uint32_t packed = frame->packed_samples[index];
        const float dut_code = (float)(uint16_t)packed - RLC_ADC_MID_CODE;
        const float tia_code = (float)(uint16_t)(packed >> 16) - RLC_ADC_MID_CODE;

        sums.cos_total += cos_ref;
        sums.sin_total += sin_ref;
        sums.cos_squared += cos_ref * cos_ref;
        sums.sin_squared += sin_ref * sin_ref;
        sums.cos_times_sin += cos_ref * sin_ref;
        AccumulateSample(&sums.dut, dut_code, cos_ref, sin_ref);
        AccumulateSample(&sums.tia, tia_code, cos_ref, sin_ref);

        const float next_cos = cos_ref * cos_step - sin_ref * sin_step;
        sin_ref = sin_ref * cos_step + cos_ref * sin_step;
        cos_ref = next_cos;
        if ((index + 1u) % RLC_REFERENCE_NORMALIZATION_INTERVAL == 0u)
        {
            /* 用一次近似倒平方根修正递推中的浮点模长漂移。 */
            const float correction = 1.5f - 0.5f * (cos_ref * cos_ref + sin_ref * sin_ref);
            cos_ref *= correction;
            sin_ref *= correction;
        }
    }
    return sums;
}

static RLC_Complex SolveChannel(ChannelSums channel, float mean_cos, float mean_sin,
                                float cos_energy, float sin_energy, float cos_sin_sum,
                                float inv_determinant)
{
    const float rhs_cos = channel.against_cos - channel.total * mean_cos;
    const float rhs_sin = channel.against_sin - channel.total * mean_sin;

    /* 对 Re(A·exp(jωt))，sin 的实系数等于 -Im(A)。 */
    const RLC_Complex codes = {(rhs_cos * sin_energy - rhs_sin * cos_sin_sum) * inv_determinant,
                               (rhs_cos * cos_sin_sum - rhs_sin * cos_energy) * inv_determinant};
    return codes;
}

bool RLC_Demodulate(const RLC_Frame *frame, RLC_Phasors *out)
{
    if (frame == NULL || out == NULL || frame->packed_samples == NULL || frame->sample_count < 4u ||
        !(frame->cycles_per_sample > 0.0f && frame->cycles_per_sample < 0.5f) ||
        !(frame->dut_volts_per_code > 0.0f && FiniteFloat(frame->dut_volts_per_code)) ||
        !(frame->tia_volts_per_code > 0.0f && FiniteFloat(frame->tia_volts_per_code)))
    {
        return false;
    }

    const FrameSums sums = AccumulateFrame(frame);
    const float inv_count = 1.0f / (float)frame->sample_count;
    const float mean_cos = sums.cos_total * inv_count;
    const float mean_sin = sums.sin_total * inv_count;
    const float cos_energy = sums.cos_squared - sums.cos_total * mean_cos;
    const float sin_energy = sums.sin_squared - sums.sin_total * mean_sin;
    const float cos_sin_sum = sums.cos_times_sin - sums.cos_total * mean_sin;
    const float determinant = cos_energy * sin_energy - cos_sin_sum * cos_sin_sum;
    const float trace = cos_energy + sin_energy;

    /* 拒绝相位跨度太小或两参考基近似共线的帧。 */
    if (!(cos_energy > 0.0f && sin_energy > 0.0f && determinant > 0.01f * trace * trace))
    {
        return false;
    }
    const float inv_determinant = 1.0f / determinant;
    const RLC_Complex dut = SolveChannel(sums.dut, mean_cos, mean_sin, cos_energy, sin_energy,
                                         cos_sin_sum, inv_determinant);
    const RLC_Complex tia = SolveChannel(sums.tia, mean_cos, mean_sin, cos_energy, sin_energy,
                                         cos_sin_sum, inv_determinant);
    const RLC_Phasors result = {
        {dut.real * frame->dut_volts_per_code, dut.imag * frame->dut_volts_per_code},
        {tia.real * frame->tia_volts_per_code, tia.imag * frame->tia_volts_per_code},
        sqrtf(dut.real * dut.real + dut.imag * dut.imag),
        sqrtf(tia.real * tia.real + tia.imag * tia.imag)};
    if (!RLC_ComplexIsFinite(result.dut_v) || !RLC_ComplexIsFinite(result.tia_v) ||
        !FiniteFloat(result.dut_peak_codes) || !FiniteFloat(result.tia_peak_codes))
    {
        return false;
    }
    *out = result;
    return true;
}

bool RLC_CalculateImpedance(RLC_Phasors phasors, RLC_Complex feedback_ohm, RLC_Complex *out)
{
    RLC_Complex voltage_ratio;
    if (out == NULL || !RLC_ComplexIsFinite(feedback_ohm) ||
        (feedback_ohm.real == 0.0f && feedback_ohm.imag == 0.0f) ||
        !RLC_ComplexDivide(phasors.dut_v, phasors.tia_v, &voltage_ratio))
    {
        return false;
    }

    /* 跨阻输出反相，Z = -Zf × Vdut / Vtia。 */
    const RLC_Complex impedance = {
        feedback_ohm.imag * voltage_ratio.imag - feedback_ohm.real * voltage_ratio.real,
        -feedback_ohm.real * voltage_ratio.imag - feedback_ohm.imag * voltage_ratio.real};
    if (!RLC_ComplexIsFinite(impedance))
    {
        return false;
    }
    *out = impedance;
    return true;
}

RLC_Component RLC_Classify(RLC_Complex impedance, float relative_phase_floor,
                           float absolute_reactance_floor_ohm)
{
    if (!RLC_ComplexIsFinite(impedance) ||
        !(relative_phase_floor >= 0.0f && FiniteFloat(relative_phase_floor)) ||
        !(absolute_reactance_floor_ohm >= 0.0f && FiniteFloat(absolute_reactance_floor_ohm)))
    {
        return RLC_COMPONENT_UNKNOWN;
    }

    const float floor_ohm =
        fmaxf(absolute_reactance_floor_ohm, relative_phase_floor * fabsf(impedance.real));
    if (impedance.imag > floor_ohm)
    {
        return RLC_COMPONENT_L;
    }
    if (impedance.imag < -floor_ohm)
    {
        return RLC_COMPONENT_C;
    }
    return impedance.real > 0.0f ? RLC_COMPONENT_R : RLC_COMPONENT_UNKNOWN;
}

bool RLC_GetComponentValue(RLC_Complex impedance, uint32_t frequency_hz, RLC_Component kind,
                           float *value_out)
{
    float value;
    if (value_out == NULL || frequency_hz == 0u || !RLC_ComplexIsFinite(impedance))
    {
        return false;
    }
    const float angular_frequency = RLC_TWO_PI * (float)frequency_hz;
    switch (kind)
    {
        case RLC_COMPONENT_R:
            value = impedance.real;
            break;
        case RLC_COMPONENT_L:
            value = impedance.imag / angular_frequency;
            break;
        case RLC_COMPONENT_C:
            if (!(impedance.imag < 0.0f))
            {
                return false;
            }
            value = (-1.0f / impedance.imag) / angular_frequency;
            break;
        default:
            return false;
    }
    if (!(value > 0.0f && FiniteFloat(value)))
    {
        return false;
    }
    *value_out = value;
    return true;
}
