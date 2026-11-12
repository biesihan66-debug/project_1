/*
 * 主机集成测试。模拟板级两路 ADC 码、夹具寄生、限流、削顶和计时。
 * TestBoard.samples[i] 的低半字为 DUT 电压、高半字为 TIA 电压。
 * 检查 RLC_Result 的 status、component、component_value、
 * frequency_hz 和 range_index，它们决定 OLED 的状态、类型、数值、频率和量程。
 * 用于验证软件路径与误差计算。硬件幅值、误差和响应时间在实物验收。
 */
#include "RLC_Measure.h"
#include "RLC_Calculate.h"
#include "BSP_Board.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_TWO_PI           6.28318530717958648f
#define TEST_MAX_SAMPLE_COUNT 60u
#define TEST_CODE_SCALE       (3.3f / 4096.0f)

typedef enum
{
    DUT_COMPONENT,
    DUT_OPEN,
    DUT_SHORT
} TestFixture;

typedef struct
{
    uint32_t samples[TEST_MAX_SAMPLE_COUNT * 24u];
    uint32_t now_ms;
    uint32_t range_mask;
    uint32_t frequency_mask;
    uint16_t stop_count;
    uint8_t frequency_index;
    uint8_t range_index;
    RLC_Component component;
    float component_value;
    float series_loss_ohm;
    float fixture_r_ohm;
    float fixture_c_f;
    TestFixture fixture;
    unsigned selections_until_timeout;
    bool fail_capture;
} TestBoard;

static TestBoard *active_board;

static void require(bool condition, const char *description)
{
    if (!condition)
    {
        fprintf(stderr, "FAIL: %s\n", description);
        exit(EXIT_FAILURE);
    }
}

static RLC_Complex inverse(RLC_Complex z)
{
    RLC_Complex out = {0};
    require(RLC_ComplexDivide((RLC_Complex){1.0f, 0.0f}, z, &out), "fixture simulation reciprocal");
    return out;
}

/* 端口模型为夹具串联电阻和被测件并联寄生电容。 */
static RLC_Complex fixture_impedance(const TestBoard *board, float omega)
{
    if (board->fixture == DUT_SHORT)
    {
        return (RLC_Complex){board->fixture_r_ohm, 0.0f};
    }
    RLC_Complex y = {0.0f, omega * board->fixture_c_f};
    if (board->fixture == DUT_COMPONENT)
    {
        RLC_Complex dut = {board->series_loss_ohm, 0.0f};
        if (board->component == RLC_COMPONENT_R)
        {
            dut.real = board->component_value;
        }
        else if (board->component == RLC_COMPONENT_L)
        {
            dut.imag = omega * board->component_value;
        }
        else
        {
            dut.imag = -1.0f / (omega * board->component_value);
        }
        const RLC_Complex y_dut = inverse(dut);
        y.real += y_dut.real;
        y.imag += y_dut.imag;
    }
    if (y.real == 0.0f && y.imag == 0.0f)
    {
        return (RLC_Complex){1.0e20f, 0.0f};
    }
    RLC_Complex z = inverse(y);
    z.real += board->fixture_r_ohm;
    return z;
}

static uint16_t adc_code(float ac, float offset, bool *clipped)
{
    float code = ac / TEST_CODE_SCALE + offset;
    if (code < 2.0f || code > 4093.0f)
    {
        *clipped = true;
        code = fminf(4095.0f, fmaxf(0.0f, code));
    }
    return (uint16_t)(code + 0.5f);
}

uint32_t BSP_GetMillis(void)
{
    return active_board->now_ms;
}

RLC_Status BSP_SelectMeasurement(uint8_t frequency, uint8_t range, uint32_t deadline)
{
    TestBoard *board = active_board;
    printf("SET %u %u %lu\n", frequency, range, (unsigned long)deadline);
    board->frequency_index = frequency;
    board->range_index = range;
    board->frequency_mask |= UINT32_C(1) << frequency;
    board->range_mask |= UINT32_C(1) << range;
    board->now_ms += 1u;
    if (board->selections_until_timeout != 0u && --board->selections_until_timeout == 0u)
    {
        board->now_ms = deadline;
        return RLC_STATUS_TIMEOUT;
    }
    return RLC_STATUS_OK;
}

const float bsp_feedback_ohm[RLC_RANGE_COUNT] = {10.0f, 100.0f, 1000.0f, 10000.0f, 100000.0f};

RLC_Status BSP_CaptureFrame(uint8_t cycles, uint32_t deadline, RLC_Frame *frame)
{
    static const uint8_t samples_per_cycle[RLC_FREQUENCY_COUNT] = {60u, 60u, 30u, 6u};
    TestBoard *board = active_board;
    const uint32_t frequency = rlc_frequency_hz[board->frequency_index];
    const uint8_t points = samples_per_cycle[board->frequency_index];
    const float omega = TEST_TWO_PI * (float)frequency;
    const RLC_Complex z = fixture_impedance(board, omega);
    const float drive_v = fminf(0.7071f, hypotf(z.real, z.imag) * 0.12f);
    RLC_Complex tia;
    if (board->fail_capture)
    {
        return RLC_STATUS_HARDWARE_ERROR;
    }
    board->now_ms += ((uint32_t)cycles * 1000u + frequency - 1u) / frequency;
    if (board->now_ms - deadline < UINT32_C(0x80000000))
    {
        return RLC_STATUS_TIMEOUT;
    }
    require(RLC_ComplexDivide((RLC_Complex){-drive_v * bsp_feedback_ohm[board->range_index], 0.0f},
                              z, &tia),
            "test transimpedance simulation");
    frame->packed_samples = board->samples;
    frame->sample_count = (size_t)cycles * points;
    frame->cycles_per_sample = 1.0f / (float)points;
    frame->dut_volts_per_code = TEST_CODE_SCALE;
    frame->tia_volts_per_code = TEST_CODE_SCALE;
    frame->dut_clipped = false;
    frame->tia_clipped = false;
    for (size_t i = 0u; i < frame->sample_count; ++i)
    {
        const float phase = 0.37f + TEST_TWO_PI * (float)(i % points) / (float)points;
        const float s = sinf(phase);
        const float c = cosf(phase);
        const uint16_t dut = adc_code(drive_v * c, 2005.0f, &frame->dut_clipped);
        const uint16_t tia_adc =
            adc_code(tia.real * c - tia.imag * s, 2080.0f, &frame->tia_clipped);
        board->samples[i] = ((uint32_t)tia_adc << 16) | dut;
    }
    return RLC_STATUS_OK;
}

void BSP_StopMeasurement(void)
{
    ++active_board->stop_count;
}

const float bsp_feedback_capacitance_f[RLC_RANGE_COUNT] = {0.0f};
const float bsp_short_min_codes = 2.0f;

static void check_reading(RLC_Result result, RLC_Component kind, float component_value,
                          float tolerance, uint32_t required_frequency, const char *name)
{
    printf("RESULT %d %d %a %lu %u %d %lu %a %a\n", result.status, result.component, (double)result.component_value, (unsigned long)result.frequency_hz, result.range_index, result.calibrated, (unsigned long)result.elapsed_ms, (double)result.impedance_ohm.real, (double)result.impedance_ohm.imag);
    const float error = fabsf(result.component_value - component_value) / component_value;
    if (result.status != RLC_STATUS_OK || result.component != kind || !(error <= tolerance) ||
        (required_frequency != 0u && result.frequency_hz != required_frequency) ||
        result.elapsed_ms >= 1000u)
    {
        fprintf(stderr, "%s: status=%d type=%d value=%.9g error=%.5g f=%lu range=%u t=%lu\n", name,
                (int)result.status, (int)result.component, (double)result.component_value,
                (double)error, (unsigned long)result.frequency_hz, (unsigned)result.range_index,
                (unsigned long)result.elapsed_ms);
        exit(EXIT_FAILURE);
    }
}

/* 验证题目边界与档位覆盖。模拟条件与真实前端误差预算独立。 */
static void test_basic_and_extended(void)
{
    static const struct
    {
        RLC_Component kind;
        float value;
        float loss;
        float tolerance;
        RLC_Mode mode;
        uint32_t required_frequency;
        const char *name;
    } cases[] = {
        {RLC_COMPONENT_R, 10.0f, 0.0f, 0.01f, RLC_MODE_BASIC, 0u, "basic R 10 ohm"},
        {RLC_COMPONENT_R, 1000.0f, 0.0f, 0.01f, RLC_MODE_BASIC, 0u, "basic R 1k"},
        {RLC_COMPONENT_R, 100000.0f, 0.0f, 0.01f, RLC_MODE_BASIC, 0u, "basic R 100k"},
        {RLC_COMPONENT_C, 1e-9f, 0.1f, 0.02f, RLC_MODE_BASIC, 10000u, "basic C 1n"},
        {RLC_COMPONENT_C, 100e-9f, 0.1f, 0.02f, RLC_MODE_BASIC, 10000u, "basic C 100n"},
        {RLC_COMPONENT_C, 10e-6f, 0.1f, 0.02f, RLC_MODE_BASIC, 100u, "basic C 10u"},
        {RLC_COMPONENT_L, 100e-6f, 1.0f, 0.02f, RLC_MODE_BASIC, 10000u, "basic L 100u"},
        {RLC_COMPONENT_L, 10e-3f, 10.0f, 0.02f, RLC_MODE_BASIC, 1000u, "basic L 10m"},
        {RLC_COMPONENT_L, 100e-3f, 30.0f, 0.02f, RLC_MODE_BASIC, 100u, "basic L 100m"},
        {RLC_COMPONENT_C, 100e-12f, 0.1f, 0.02f, RLC_MODE_EXTENDED, 100000u, "extended C 100p"},
        {RLC_COMPONENT_C, 100e-9f, 0.1f, 0.02f, RLC_MODE_EXTENDED, 100000u, "extended C 100n"},
        {RLC_COMPONENT_L, 10e-6f, 4.0f, 0.02f, RLC_MODE_EXTENDED, 100000u, "extended L 10u"},
        {RLC_COMPONENT_L, 1e-3f, 4.0f, 0.02f, RLC_MODE_EXTENDED, 100000u, "extended L 1m"},
        {RLC_COMPONENT_R, 10.0f, 0.0f, 0.005f, RLC_MODE_EXTENDED, 0u, "wide R 10 ohm"},
        {RLC_COMPONENT_R, 1e6f, 0.0f, 0.005f, RLC_MODE_EXTENDED, 100u, "wide R 1M"}};
    uint32_t selected = 0u;
    for (size_t i = 0u; i < sizeof cases / sizeof cases[0]; ++i)
    {
        TestBoard board = {.component = cases[i].kind,
                           .component_value = cases[i].value,
                           .series_loss_ohm = cases[i].loss};
        active_board = &board;
        const RLC_Result result = RLC_MeasureAuto(NULL, cases[i].mode);
        check_reading(result, cases[i].kind, cases[i].value, cases[i].tolerance,
                      cases[i].required_frequency, cases[i].name);
        require(board.stop_count == 1u, "measurement stops source once");
        if (cases[i].mode == RLC_MODE_BASIC)
        {
            require((board.frequency_mask & (UINT32_C(1) << RLC_FREQUENCY_100_KHZ)) == 0u,
                    "basic mode never touches 100k");
        }
        selected |= UINT32_C(1) << result.range_index;
    }
    require((selected & (selected - 1u)) != 0u, "automatic range varies across test cases");
    unsigned range_count = 0u;
    for (uint8_t range = 0u; range < RLC_RANGE_COUNT; ++range)
    {
        range_count += (unsigned)((selected >> range) & 1u);
    }
    require(range_count >= 3u, "automatic measurement selected at least three distinct ranges");
}

static void test_calibration(void)
{
    TestBoard board = {.fixture_r_ohm = 0.05f, .fixture_c_f = 3e-12f, .fixture = DUT_OPEN};
    RLC_Calibration calibration = {0};
    active_board = &board;
    require(RLC_CaptureCalibration(&calibration, true, RLC_MODE_EXTENDED) == RLC_STATUS_OK,
            "open acquisition");
    board.fixture = DUT_SHORT;
    require(RLC_CaptureCalibration(&calibration, false, RLC_MODE_EXTENDED) == RLC_STATUS_OK,
            "short acquisition");
    require(Calibration_HasPair(&calibration, RLC_FREQUENCY_100_KHZ, 4u),
            "calibration available in the highest range");
    board.fixture = DUT_COMPONENT;
    board.component = RLC_COMPONENT_C;
    board.component_value = 10e-12f;
    const RLC_Result corrected = RLC_MeasureAuto(&calibration, RLC_MODE_EXTENDED);
    check_reading(corrected, RLC_COMPONENT_C, 10e-12f, 0.05f, 100000u, "compensated 10pF");
    require(corrected.calibrated, "successful small-C result is flagged as calibrated");

    const RLC_Calibration before = calibration;
    /* 让首个档位从理想开路改为实测开路，再在第二次选档时超时。 */
    board.fixture = DUT_OPEN;
    board.fixture_c_f = 3e-6f;
    board.selections_until_timeout = 2u;
    require(RLC_CaptureCalibration(&calibration, true, RLC_MODE_EXTENDED) == RLC_STATUS_TIMEOUT,
            "aborted calibration returns timeout");
    require(memcmp(&before, &calibration, sizeof calibration) == 0,
            "aborted calibration keeps prior record");
}

static void test_basic_calibration_frequency_limit(void)
{
    TestBoard board = {.fixture_r_ohm = 0.05f, .fixture_c_f = 3e-12f, .fixture = DUT_OPEN};
    RLC_Calibration calibration = {0};
    active_board = &board;
    require(RLC_CaptureCalibration(&calibration, true, RLC_MODE_BASIC) == RLC_STATUS_OK,
            "basic open acquisition");
    board.fixture = DUT_SHORT;
    require(RLC_CaptureCalibration(&calibration, false, RLC_MODE_BASIC) == RLC_STATUS_OK,
            "basic short acquisition");
    require(board.frequency_mask == UINT32_C(0x7),
            "basic calibration touches exactly three frequencies");
    require(Calibration_HasPair(&calibration, RLC_FREQUENCY_10_KHZ, 4u),
            "basic calibration records highest base frequency");
    require(!Calibration_HasPair(&calibration, RLC_FREQUENCY_100_KHZ, 4u),
            "basic calibration leaves extended frequency unset");
}

/* 对数扫描覆盖量程交界和选频阈值两侧，检验相邻试测设置会否跳错档。 */
static void test_range_sweep(void)
{
    static const struct
    {
        RLC_Mode mode;
        RLC_Component component;
        float minimum;
        float maximum;
        float tolerance;
        float maximum_value_at_100khz;
        const char *description;
    } sweeps[] = {
        {RLC_MODE_BASIC, RLC_COMPONENT_R, 10.0f, 100e3f, 0.01f, 0.0f, "basic R sweep"},
        {RLC_MODE_BASIC, RLC_COMPONENT_L, 100e-6f, 100e-3f, 0.02f, 0.0f, "basic L sweep"},
        {RLC_MODE_BASIC, RLC_COMPONENT_C, 1e-9f, 10e-6f, 0.02f, 0.0f, "basic C sweep"},
        {RLC_MODE_EXTENDED, RLC_COMPONENT_R, 10.0f, 1e6f, 0.005f, 0.0f, "extended R sweep"},
        {RLC_MODE_EXTENDED, RLC_COMPONENT_L, 10e-6f, 100e-3f, 0.02f, 1e-3f, "extended L sweep"},
        {RLC_MODE_EXTENDED, RLC_COMPONENT_C, 100e-12f, 10e-6f, 0.02f, 100e-9f, "extended C sweep"}};
    for (size_t i = 0u; i < sizeof sweeps / sizeof sweeps[0]; ++i)
    {
        for (unsigned step = 0u; step <= 600u; ++step)
        {
            const float value = sweeps[i].minimum *
                                powf(sweeps[i].maximum / sweeps[i].minimum, (float)step / 600.0f);
            TestBoard board = {.component = sweeps[i].component,
                               .component_value = value,
                               .series_loss_ohm = sweeps[i].component == RLC_COMPONENT_L
                                                      ? 1.0f + 300.0f * value
                                                      : 0.1f};
            active_board = &board;
            const uint32_t expected_frequency =
                value <= sweeps[i].maximum_value_at_100khz ? 100000u : 0u;
            const RLC_Result result = RLC_MeasureAuto(NULL, sweeps[i].mode);
            check_reading(result, board.component, value, sweeps[i].tolerance, expected_frequency,
                          sweeps[i].description);
            if (sweeps[i].mode == RLC_MODE_BASIC)
            {
                require((board.frequency_mask & (UINT32_C(1) << RLC_FREQUENCY_100_KHZ)) == 0u,
                        "basic sweep never touches 100 kHz");
            }
        }
    }
}

/* 非整数周期窗口：两路直流偏置不同，验证解调结果不受窗口余量和偏置影响。
 * 四组采样节拍对应四个频段；频率略偏离标称值，数据直接由已知幅度和相位生成。
 * 这是算法输入的验证，不代表实物前端已达到同样精度。 */
static void test_fractional_cycle_demodulation(void)
{
    static const struct
    {
        float signal_hz;
        float sample_rate_hz;
        size_t sample_count;
    } cases[] = {{99.987f, 6000.0f, 953u},
                 {999.987f, 60000.0f, 953u},
                 {10000.021f, 300000.0f, 473u},
                 {99999.99f, 600000.0f, 89u}};
    const double dut_amplitude_v = 0.54;
    const double tia_amplitude_v = 0.23;
    const double dut_phase = -0.37;
    const double tia_phase = 2.12;
    const double two_pi = 6.2831853071795864769;
    uint32_t samples[953];

    for (size_t case_index = 0u; case_index < sizeof cases / sizeof cases[0]; ++case_index)
    {
        RLC_Frame frame = {.packed_samples = samples,
                           .sample_count = cases[case_index].sample_count,
                           .cycles_per_sample =
                               cases[case_index].signal_hz / cases[case_index].sample_rate_hz,
                           .dut_volts_per_code = TEST_CODE_SCALE,
                           .tia_volts_per_code = TEST_CODE_SCALE};
        for (size_t sample_index = 0u; sample_index < frame.sample_count; ++sample_index)
        {
            const double phase = two_pi * frame.cycles_per_sample * (double)sample_index;
            const uint16_t dut = adc_code((float)(dut_amplitude_v * cos(phase + dut_phase)),
                                          1750.0f, &frame.dut_clipped);
            const uint16_t tia = adc_code((float)(tia_amplitude_v * cos(phase + tia_phase)),
                                          2300.0f, &frame.tia_clipped);
            samples[sample_index] = ((uint32_t)tia << 16) | dut;
        }
        RLC_Phasors measured;
        require(!frame.dut_clipped && !frame.tia_clipped,
                "fractional-window fixture does not clip");
        require(RLC_Demodulate(&frame, &measured), "fractional-window demodulation succeeds");
        require(fabs(measured.dut_v.real - dut_amplitude_v * cos(dut_phase)) <= 0.0005 &&
                    fabs(measured.dut_v.imag - dut_amplitude_v * sin(dut_phase)) <= 0.0005 &&
                    fabs(measured.tia_v.real - tia_amplitude_v * cos(tia_phase)) <= 0.0005 &&
                    fabs(measured.tia_v.imag - tia_amplitude_v * sin(tia_phase)) <= 0.0005,
                "fractional-window voltage and phase agree with independent input");
    }
}

static void test_fixed_and_faults(void)
{
    TestBoard board = {
        .now_ms = UINT32_MAX - 15u, .component = RLC_COMPONENT_R, .component_value = 1000.0f};
    active_board = &board;
    const RLC_Result fixed = RLC_MeasureFixed(NULL, RLC_FREQUENCY_1_KHZ, RLC_RANGE_1_KOHM);
    check_reading(fixed, RLC_COMPONENT_R, 1000.0f, 0.01f, 1000u, "fixed test across timer wrap");
    require(fixed.elapsed_ms == 17u, "elapsed time computed across timer wrap");
    board.fixture = DUT_OPEN;
    require(RLC_MeasureAuto(NULL, RLC_MODE_EXTENDED).status == RLC_STATUS_LOW_SIGNAL,
            "open without a measurable leakage returns low signal");
    board.selections_until_timeout = 1u;
    require(RLC_MeasureAuto(NULL, RLC_MODE_BASIC).status == RLC_STATUS_TIMEOUT,
            "timed out acquisition propagates");
    board.fail_capture = true;
    require(RLC_MeasureAuto(NULL, RLC_MODE_EXTENDED).status == RLC_STATUS_HARDWARE_ERROR,
            "capture hardware fault propagates");
    require(board.stop_count == 4u, "normal and failed measurements leave the source stopped");
}

int main(void)
{
    test_basic_and_extended();
    test_calibration();
    test_basic_calibration_frequency_limit();
    test_fixed_and_faults();
    test_range_sweep();
    test_fractional_cycle_demodulation();
    puts("PASS: RLC limits, 3606 basic/extended sweep cases, fractional-cycle demodulation, "
         "calibration, time wrap, and faults");
    return EXIT_SUCCESS;
}
