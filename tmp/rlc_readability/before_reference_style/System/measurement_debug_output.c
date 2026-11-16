/*
 * 电桥串口诊断。USART 引脚和波特率在 board_pin_map.h，默认 TX 为 PA9。
 * text_buffer 在本文件静态存储，函数每次从下标 0 重写并立即发送。
 * result->phasors 包含 DUT/跨阻两路的 I/Q 电压和 ADC 峰值码，
 * result->impedance_ohm 包含实部和虚部。行内字段名标出 SI 单位。
 * OLED 第 1 行对应 component/status，第 2 行对应 value_si，
 * 第 3 行对应 frequency_hz，第 4 行对应 range_index/calibrated/status。
 * core_ms 由测量模块记录，display_ms 从主循环接到测量命令开始，直至 OLED 更新返回。
 */
#include "measurement_debug_output.h"

#include "measurement_text_format.h"
#include "stm32_rlc_board.h"

static char text_buffer[384];

static char component_code(RlcComponent component)
{
    switch (component) {
    case RLC_COMPONENT_R: return 'R';
    case RLC_COMPONENT_L: return 'L';
    case RLC_COMPONENT_C: return 'C';
    default: return '?';
    }
}

static void append_unsigned_field(RlcText *text, const char *name, uint32_t value)
{
    rlc_text_append(text, name);
    rlc_text_unsigned(text, value);
}

static void append_float_field(RlcText *text, const char *name, float value)
{
    rlc_text_append(text, name);
    rlc_text_scientific(text, value);
}

void measurement_debug_output_result(const RlcMeasurementResult *result,
                                     uint32_t display_elapsed_ms, bool display_ok)
{
    RlcText text = rlc_text_begin(text_buffer, sizeof text_buffer);

    append_unsigned_field(&text, "MEASURE status=", (uint32_t)result->status);
    rlc_text_append(&text, " type=");
    rlc_text_char(&text, component_code(result->component));
    append_float_field(&text, " value_si=", result->value_si);
    append_unsigned_field(&text, " frequency_hz=", result->frequency_hz);
    append_unsigned_field(&text, " range_index=", result->range_index);
    append_unsigned_field(&text, " calibrated=", result->calibrated ? 1u : 0u);
    append_unsigned_field(&text, " core_ms=", result->elapsed_ms);
    append_unsigned_field(&text, " display_ms=", display_elapsed_ms);
    append_unsigned_field(&text, " display_ok=", display_ok ? 1u : 0u);
    append_float_field(&text, " z_re_ohm=", result->impedance_ohm.re);
    append_float_field(&text, " z_im_ohm=", result->impedance_ohm.im);
    append_float_field(&text, " dut_i_v=", result->phasors.dut_v.re);
    append_float_field(&text, " dut_q_v=", result->phasors.dut_v.im);
    append_float_field(&text, " tia_i_v=", result->phasors.tia_v.re);
    append_float_field(&text, " tia_q_v=", result->phasors.tia_v.im);
    append_float_field(&text, " dut_peak_codes=", result->phasors.dut_peak_codes);
    append_float_field(&text, " tia_peak_codes=", result->phasors.tia_peak_codes);
    rlc_text_append(&text, "\r\n");
    rlc_board_debug_write(text.buffer);
}

void measurement_debug_output_calibration(bool open, RlcStatus status)
{
    RlcText text = rlc_text_begin(text_buffer, sizeof text_buffer);

    rlc_text_append(&text, open ? "CAL OPEN" : "CAL SHORT");
    append_unsigned_field(&text, " status=", (uint32_t)status);
    rlc_text_append(&text, "\r\n");
    rlc_board_debug_write(text.buffer);
}
