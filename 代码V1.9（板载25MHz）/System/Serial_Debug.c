/*
 * 电桥串口诊断。USART 引脚和波特率在 BSP_PinMap.h，默认 TX 为 PA9。
 * text_buffer 在本文件静态存储，函数每次从下标 0 重写并立即发送。
 * result->phasors 包含 DUT/跨阻两路的 I/Q 电压和 ADC 峰值码，
 * result->impedance_ohm 包含实部和虚部。行内字段名标出 SI 单位。
 * OLED 第 1 行对应 component/status，第 2 行对应 component_value，
 * 第 3 行对应 frequency_hz，第 4 行对应 range_index/calibrated/status。
 * core_ms 由测量模块记录，display_ms 从主循环接到测量命令开始，直至 OLED 更新返回。
 */
#include "Serial_Debug.h"

#include "Text_Format.h"
#include "BSP_Board.h"

static char text_buffer[448];

static char ComponentCode(RLC_Component component)
{
    switch (component)
    {
        case RLC_COMPONENT_R:
            return 'R';
        case RLC_COMPONENT_L:
            return 'L';
        case RLC_COMPONENT_C:
            return 'C';
        default:
            return '?';
    }
}

static void AppendUnsignedField(TextBuffer *text, const char *name, uint32_t value)
{
    Text_Append(text, name);
    Text_AddUnsigned(text, value);
}

static void AppendFloatField(TextBuffer *text, const char *name, float value)
{
    Text_Append(text, name);
    Text_AddScientific(text, value);
}

void Serial_PrintResult(const RLC_Result *result, uint32_t display_elapsed_ms, uint32_t press_elapsed_ms, bool display_ok)
{
    TextBuffer text = Text_Begin(text_buffer, sizeof text_buffer);

    AppendUnsignedField(&text, "MEASURE status=", (uint32_t)result->status);
    Text_Append(&text, " type=");
    Text_AddChar(&text, ComponentCode(result->component));
    AppendFloatField(&text, " value_si=", result->component_value);
    AppendUnsignedField(&text, " frequency_hz=", result->frequency_hz);
    AppendUnsignedField(&text, " range_index=", result->range_index);
    AppendUnsignedField(&text, " calibrated=", result->calibrated ? 1u : 0u);
    AppendUnsignedField(&text, " core_ms=", result->elapsed_ms);
    AppendUnsignedField(&text, " display_ms=", display_elapsed_ms);
    AppendUnsignedField(&text, " press_to_oled_ms=", press_elapsed_ms);
    if (result->fault != NULL)
    {
        Text_Append(&text, " fault=");
        Text_Append(&text, result->fault);
    }
    AppendUnsignedField(&text, " display_ok=", display_ok ? 1u : 0u);
    AppendFloatField(&text, " z_re_ohm=", result->impedance_ohm.real);
    AppendFloatField(&text, " z_im_ohm=", result->impedance_ohm.imag);
    AppendFloatField(&text, " dut_i_v=", result->phasors.dut_v.real);
    AppendFloatField(&text, " dut_q_v=", result->phasors.dut_v.imag);
    AppendFloatField(&text, " tia_i_v=", result->phasors.tia_v.real);
    AppendFloatField(&text, " tia_q_v=", result->phasors.tia_v.imag);
    AppendFloatField(&text, " dut_peak_codes=", result->phasors.dut_peak_codes);
    AppendFloatField(&text, " tia_peak_codes=", result->phasors.tia_peak_codes);
    Text_Append(&text, "\r\n");
    BSP_DebugWrite(text.buffer);
}

void Serial_PrintCalibration(bool open, RLC_Status status)
{
    TextBuffer text = Text_Begin(text_buffer, sizeof text_buffer);

    Text_Append(&text, open ? "CAL OPEN" : "CAL SHORT");
    AppendUnsignedField(&text, " status=", (uint32_t)status);
    Text_Append(&text, "\r\n");
    BSP_DebugWrite(text.buffer);
}
