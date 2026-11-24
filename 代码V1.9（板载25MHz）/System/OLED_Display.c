/*
 * RLC 页面排版。显示驱动和字模复用江协科技的 OLED.c / OLED_Data.c。
 * 逻辑行 1 至 4 对应 y=0、16、32、48 像素，使用 6×8 字模。
 * value_line、frequency_line、range_line 分别是第 2、3、4 行文本。
 * result->component 决定第 1 行，测量失败时第 1 行显示原因，第 4 行提示重测。
 */
#include "OLED_Display.h"
#include "OLED.h"
#include "Text_Format.h"

enum
{
    OLED_TYPE_Y = 0u,
    OLED_VALUE_Y = 16u,
    OLED_FREQUENCY_Y = 32u,
    OLED_RANGE_Y = 48u,
    OLED_WIDTH = 128u,
    OLED_ROW_HEIGHT = 8u,
    OLED_TEXT_CAPACITY = OLED_WIDTH / OLED_6X8 + 1u
};

bool OLED_DisplayInit(void)
{
    OLED_Init();
    return OLED_GetLastError() == OLED_ERROR_NONE;
}

/* 刷新一页前先清本行显存，以免短字符串残留上一帧尾部。 */
static bool RenderLine(uint8_t y, const char *line)
{
    OLED_ClearArea(0, y, OLED_WIDTH, OLED_ROW_HEIGHT);
    OLED_ShowString(0, y, line, OLED_6X8);
    OLED_UpdateArea(0, y, OLED_WIDTH, OLED_ROW_HEIGHT);
    return OLED_GetLastError() == OLED_ERROR_NONE;
}

bool OLED_ShowLines(const char *line1, const char *line2, const char *line3, const char *line4)
{
    return RenderLine(OLED_TYPE_Y, line1) && RenderLine(OLED_VALUE_Y, line2) &&
           RenderLine(OLED_FREQUENCY_Y, line3) && RenderLine(OLED_RANGE_Y, line4);
}

bool OLED_ShowActivity(const char *message)
{
    return OLED_ShowLines(message, "", "", "");
}

const char *OLED_GetStatusText(RLC_Status status)
{
    switch (status)
    {
        case RLC_STATUS_OK:
            return "OK";
        case RLC_STATUS_TIMEOUT:
            return "TIMEOUT";
        case RLC_STATUS_OVERLOAD:
            return "OVERLOAD";
        case RLC_STATUS_LOW_SIGNAL:
            return "LOW SIGNAL";
        case RLC_STATUS_AMBIGUOUS:
            return "TYPE UNCERTAIN";
        case RLC_STATUS_CALIBRATION_ERROR:
            return "CAL DATA ERROR";
        case RLC_STATUS_HARDWARE_ERROR:
            return "HARDWARE ERROR";
        default:
            return "UNKNOWN ERROR";
    }
}

/* 第 2、4 行共用，value 经过工程单位缩放后至多显示 3 位小数。 */
static void AppendValueAndUnit(TextBuffer *text, float value, const char *unit)
{
    if (!(value >= 0.0f && value <= 1000000.0f))
    {
        Text_Append(text, "---");
        return;
    }
    Text_AddDecimal(text, (uint32_t)(value * 1000.0f + 0.5f), 1000u);
    Text_AddChar(text, ' ');
    Text_Append(text, unit);
}

static void AppendResistance(TextBuffer *text, float ohm)
{
    if (ohm >= 1000000.0f)
    {
        AppendValueAndUnit(text, ohm * 0.000001f, "MOHM");
    }
    else if (ohm >= 1000.0f)
    {
        AppendValueAndUnit(text, ohm * 0.001f, "kOHM");
    }
    else
    {
        AppendValueAndUnit(text, ohm, "OHM");
    }
}

static void FormatMeasurementValue(TextBuffer *text, const RLC_Result *result)
{
    const float value = result->component_value;

    switch (result->component)
    {
        case RLC_COMPONENT_R:
            AppendResistance(text, value);
            break;
        case RLC_COMPONENT_L:
            if (value >= 1.0f)
            {
                AppendValueAndUnit(text, value, "H");
            }
            else if (value >= 0.001f)
            {
                AppendValueAndUnit(text, value * 1000.0f, "mH");
            }
            else
            {
                AppendValueAndUnit(text, value * 1000000.0f, "uH");
            }
            break;
        case RLC_COMPONENT_C:
            if (value >= 0.000001f)
            {
                AppendValueAndUnit(text, value * 1000000.0f, "uF");
            }
            else if (value >= 0.000000001f)
            {
                AppendValueAndUnit(text, value * 1000000000.0f, "nF");
            }
            else
            {
                AppendValueAndUnit(text, value * 1000000000000.0f, "pF");
            }
            break;
        default:
            Text_Append(text, "---");
            break;
    }
}

static const char *component_text(RLC_Component component)
{
    switch (component)
    {
        case RLC_COMPONENT_R:
            return "R  RESISTANCE";
        case RLC_COMPONENT_L:
            return "L  INDUCTANCE";
        case RLC_COMPONENT_C:
            return "C  CAPACITANCE";
        default:
            return "UNKNOWN TYPE";
    }
}

bool OLED_ShowResult(const RLC_Result *result, float feedback_ohm)
{
    char value_line[OLED_TEXT_CAPACITY];
    char frequency_line[OLED_TEXT_CAPACITY];
    char range_line[OLED_TEXT_CAPACITY];
    TextBuffer value = Text_Begin(value_line, sizeof value_line);
    TextBuffer frequency = Text_Begin(frequency_line, sizeof frequency_line);
    TextBuffer range = Text_Begin(range_line, sizeof range_line);

    if (result->status != RLC_STATUS_OK)
    {
        return OLED_ShowLines(OLED_GetStatusText(result->status), "", "", "PRESS TO RETRY");
    }

    if (result->frequency_hz >= 1000u && result->frequency_hz % 1000u == 0u)
    {
        Text_AddUnsigned(&frequency, result->frequency_hz / 1000u);
        Text_Append(&frequency, " kHz");
    }
    else if (result->frequency_hz != 0u)
    {
        Text_AddUnsigned(&frequency, result->frequency_hz);
        Text_Append(&frequency, " Hz");
    }
    else
    {
        Text_Append(&frequency, "---");
    }

    FormatMeasurementValue(&value, result);
    Text_Append(&range, "RF ");
    AppendResistance(&range, feedback_ohm);
    Text_Append(&range, result->calibrated ? " CAL" : " RAW");
    return OLED_ShowLines(component_text(result->component), value_line, frequency_line,
                          range_line);
}
