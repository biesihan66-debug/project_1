/*
 * SSD1306 128x64 文本显示。PB6/SCL、PB7/SDA 和 7 位地址由 board_pin_map.h 定义。
 * 逻辑行 1 至 4 对应 GDDRAM 页 0、2、4、6，起始 y 为 0、16、32、48 像素。
 * font_letters[c-'A']、font_digits[c-'0'] 各存 5 列像素，低位对应上方像素。
 * render_line() 的 pixels[x] 是一页的第 x 列，下一字形前留 1 列空白。
 * oled_result_display_show_measurement() 中的 value_line、frequency_line、range_line
 * 分别送到第 2、3、4 行，
 * result->component 决定第 1 行，result->status 的失败提示在第 4 行。
 */
#include "oled_result_display.h"

#include "measurement_text_format.h"
#include "stm32_rlc_board.h"

enum {
    OLED_WIDTH = 128u,
    OLED_PAGE_COUNT = 8u,
    OLED_TYPE_PAGE = 0u,
    OLED_VALUE_PAGE = 2u,
    OLED_FREQUENCY_PAGE = 4u,
    OLED_RANGE_PAGE = 6u,
    OLED_GLYPH_WIDTH = 5u,
    OLED_CHARACTER_WIDTH = 6u,
    OLED_TEXT_CAPACITY = OLED_WIDTH / OLED_CHARACTER_WIDTH + 1u,
    SSD1306_COMMAND_CONTROL = 0x00u,
    SSD1306_DATA_CONTROL = 0x40u
};

/* initialized 只由初始化函数更新，发送行数据前检查。 */
static bool initialized;

/* SSD1306 列方向像素字形，存放在 Flash，取 5 字节后留一列间隔。 */
static const uint8_t font_letters[26][OLED_GLYPH_WIDTH] = {
    {0x7e, 0x11, 0x11, 0x11, 0x7e}, {0x7f, 0x49, 0x49, 0x49, 0x36},
    {0x3e, 0x41, 0x41, 0x41, 0x22}, {0x7f, 0x41, 0x41, 0x22, 0x1c},
    {0x7f, 0x49, 0x49, 0x49, 0x41}, {0x7f, 0x09, 0x09, 0x09, 0x01},
    {0x3e, 0x41, 0x49, 0x49, 0x7a}, {0x7f, 0x08, 0x08, 0x08, 0x7f},
    {0x00, 0x41, 0x7f, 0x41, 0x00}, {0x20, 0x40, 0x41, 0x3f, 0x01},
    {0x7f, 0x08, 0x14, 0x22, 0x41}, {0x7f, 0x40, 0x40, 0x40, 0x40},
    {0x7f, 0x02, 0x0c, 0x02, 0x7f}, {0x7f, 0x04, 0x08, 0x10, 0x7f},
    {0x3e, 0x41, 0x41, 0x41, 0x3e}, {0x7f, 0x09, 0x09, 0x09, 0x06},
    {0x3e, 0x41, 0x51, 0x21, 0x5e}, {0x7f, 0x09, 0x19, 0x29, 0x46},
    {0x46, 0x49, 0x49, 0x49, 0x31}, {0x01, 0x01, 0x7f, 0x01, 0x01},
    {0x3f, 0x40, 0x40, 0x40, 0x3f}, {0x1f, 0x20, 0x40, 0x20, 0x1f},
    {0x7f, 0x20, 0x18, 0x20, 0x7f}, {0x63, 0x14, 0x08, 0x14, 0x63},
    {0x07, 0x08, 0x70, 0x08, 0x07}, {0x61, 0x51, 0x49, 0x45, 0x43}
};

static const uint8_t font_digits[10][OLED_GLYPH_WIDTH] = {
    {0x3e, 0x51, 0x49, 0x45, 0x3e}, {0x00, 0x42, 0x7f, 0x40, 0x00},
    {0x42, 0x61, 0x51, 0x49, 0x46}, {0x21, 0x41, 0x45, 0x4b, 0x31},
    {0x18, 0x14, 0x12, 0x7f, 0x10}, {0x27, 0x45, 0x45, 0x45, 0x39},
    {0x3c, 0x4a, 0x49, 0x49, 0x30}, {0x01, 0x71, 0x09, 0x05, 0x03},
    {0x36, 0x49, 0x49, 0x49, 0x36}, {0x06, 0x49, 0x49, 0x29, 0x1e}
};

static const uint8_t *glyph_columns(char character)
{
    static const uint8_t blank[OLED_GLYPH_WIDTH] = {0};
    static const uint8_t period[OLED_GLYPH_WIDTH] = {0, 0x60, 0x60, 0, 0};
    static const uint8_t hyphen[OLED_GLYPH_WIDTH] = {0x08, 0x08, 0x08, 0x08, 0x08};
    static const uint8_t slash[OLED_GLYPH_WIDTH] = {0x20, 0x10, 0x08, 0x04, 0x02};
    static const uint8_t small_k[OLED_GLYPH_WIDTH] = {0x7f, 0x10, 0x28, 0x44, 0};
    static const uint8_t small_m[OLED_GLYPH_WIDTH] = {0x7c, 0x04, 0x38, 0x04, 0x78};
    static const uint8_t small_n[OLED_GLYPH_WIDTH] = {0x7c, 0x08, 0x04, 0x04, 0x78};
    static const uint8_t small_p[OLED_GLYPH_WIDTH] = {0xfc, 0x24, 0x24, 0x24, 0x18};
    static const uint8_t small_u[OLED_GLYPH_WIDTH] = {0x3c, 0x40, 0x40, 0x20, 0x7c};
    static const uint8_t small_z[OLED_GLYPH_WIDTH] = {0x44, 0x64, 0x54, 0x4c, 0x44};

    if (character >= 'A' && character <= 'Z') {
        return font_letters[character - 'A'];
    }
    if (character >= '0' && character <= '9') {
        return font_digits[character - '0'];
    }
    if (character == '.') {
        return period;
    }
    if (character == '-') {
        return hyphen;
    }
    if (character == '/') {
        return slash;
    }
    switch (character) {
    case 'k': return small_k;
    case 'm': return small_m;
    case 'n': return small_n;
    case 'p': return small_p;
    case 'u': return small_u;
    case 'z': return small_z;
    default: return blank;
    }
}

/* SSD1306 页模式，每次完整覆盖 128 列以抹掉旧行右侧字符。 */
static bool render_line(uint8_t page, const char *line)
{
    const uint8_t address[] = {(uint8_t)(0xb0u + page), 0x00u, 0x10u};
    uint8_t pixels[OLED_WIDTH] = {0};
    size_t column = 0u;

    while (*line != '\0' && column + OLED_CHARACTER_WIDTH <= OLED_WIDTH) {
        const uint8_t *glyph = glyph_columns(*line++);
        for (size_t i = 0u; i < OLED_GLYPH_WIDTH; ++i) {
            pixels[column + i] = glyph[i];
        }
        column += OLED_CHARACTER_WIDTH;
    }

    return rlc_board_oled_write(SSD1306_COMMAND_CONTROL, address, sizeof address)
        && rlc_board_oled_write(SSD1306_DATA_CONTROL, pixels, sizeof pixels);
}

bool oled_result_display_initialize(void)
{
    static const uint8_t setup[] = {
        0xae, 0xd5, 0x80, 0xa8, 0x3f, 0xd3, 0x00, 0x40,
        0x8d, 0x14, 0x20, 0x02, 0xa1, 0xc8, 0xda, 0x12,
        0x81, 0x8f, 0xd9, 0xf1, 0xdb, 0x40, 0xa4, 0xa6, 0x2e
    };
    static const uint8_t display_on[] = {0xaf};

    initialized = false;
    if (!rlc_board_oled_write(SSD1306_COMMAND_CONTROL, setup, sizeof setup)) {
        return false;
    }
    for (uint8_t page = 0u; page < OLED_PAGE_COUNT; ++page) {
        if (!render_line(page, "")) {
            return false;
        }
    }
    initialized = rlc_board_oled_write(SSD1306_COMMAND_CONTROL,
                                      display_on, sizeof display_on);
    return initialized;
}

bool oled_result_display_show_lines(const char *line1, const char *line2,
                                    const char *line3, const char *line4)
{
    return initialized
        && render_line(OLED_TYPE_PAGE, line1)
        && render_line(OLED_VALUE_PAGE, line2)
        && render_line(OLED_FREQUENCY_PAGE, line3)
        && render_line(OLED_RANGE_PAGE, line4);
}

/* 测量启动时只写第 1 行，把其余行的刷屏留到结果返回时。 */
bool oled_result_display_show_activity(const char *message)
{
    return initialized && render_line(OLED_TYPE_PAGE, message);
}

const char *oled_result_display_status_text(RlcStatus status)
{
    switch (status) {
    case RLC_STATUS_OK: return "OK";
    case RLC_STATUS_TIMEOUT: return "TIMEOUT";
    case RLC_STATUS_OVERLOAD: return "OVERLOAD";
    case RLC_STATUS_LOW_SIGNAL: return "LOW SIGNAL";
    case RLC_STATUS_AMBIGUOUS: return "TYPE UNCERTAIN";
    case RLC_STATUS_CALIBRATION_ERROR: return "CAL DATA ERROR";
    case RLC_STATUS_HARDWARE_ERROR: return "HARDWARE ERROR";
    default: return "UNKNOWN ERROR";
    }
}

/* 第 2、4 行共用，value 经过工程单位缩放后至多显示 3 位小数。 */
static void append_value_and_unit(RlcText *text, float value, const char *unit)
{
    if (!(value >= 0.0f && value <= 1000000.0f)) {
        rlc_text_append(text, "---");
        return;
    }
    rlc_text_decimal(text, (uint32_t)(value * 1000.0f + 0.5f), 1000u);
    rlc_text_char(text, ' ');
    rlc_text_append(text, unit);
}

static void append_resistance(RlcText *text, float ohm)
{
    if (ohm >= 1000000.0f) {
        append_value_and_unit(text, ohm * 0.000001f, "MOHM");
    } else if (ohm >= 1000.0f) {
        append_value_and_unit(text, ohm * 0.001f, "kOHM");
    } else {
        append_value_and_unit(text, ohm, "OHM");
    }
}

static void format_measurement_value(RlcText *text, const RlcMeasurementResult *result)
{
    const float value = result->value_si;

    switch (result->component) {
    case RLC_COMPONENT_R:
        append_resistance(text, value);
        break;
    case RLC_COMPONENT_L:
        if (value >= 1.0f) {
            append_value_and_unit(text, value, "H");
        } else if (value >= 0.001f) {
            append_value_and_unit(text, value * 1000.0f, "mH");
        } else {
            append_value_and_unit(text, value * 1000000.0f, "uH");
        }
        break;
    case RLC_COMPONENT_C:
        if (value >= 0.000001f) {
            append_value_and_unit(text, value * 1000000.0f, "uF");
        } else if (value >= 0.000000001f) {
            append_value_and_unit(text, value * 1000000000.0f, "nF");
        } else {
            append_value_and_unit(text, value * 1000000000000.0f, "pF");
        }
        break;
    default:
        rlc_text_append(text, "---");
        break;
    }
}

static const char *component_text(RlcComponent component)
{
    switch (component) {
    case RLC_COMPONENT_R: return "R  RESISTANCE";
    case RLC_COMPONENT_L: return "L  INDUCTANCE";
    case RLC_COMPONENT_C: return "C  CAPACITANCE";
    default: return "UNKNOWN TYPE";
    }
}

bool oled_result_display_show_measurement(const RlcMeasurementResult *result,
                                          float feedback_ohm)
{
    char value_line[OLED_TEXT_CAPACITY];
    char frequency_line[OLED_TEXT_CAPACITY];
    char range_line[OLED_TEXT_CAPACITY];
    RlcText value = rlc_text_begin(value_line, sizeof value_line);
    RlcText frequency = rlc_text_begin(frequency_line, sizeof frequency_line);
    RlcText range = rlc_text_begin(range_line, sizeof range_line);

    if (result->frequency_hz >= 1000u && result->frequency_hz % 1000u == 0u) {
        rlc_text_unsigned(&frequency, result->frequency_hz / 1000u);
        rlc_text_append(&frequency, " kHz");
    } else if (result->frequency_hz != 0u) {
        rlc_text_unsigned(&frequency, result->frequency_hz);
        rlc_text_append(&frequency, " Hz");
    } else {
        rlc_text_append(&frequency, "---");
    }

    if (result->status != RLC_STATUS_OK) {
        return oled_result_display_show_lines("MEASURE ERROR", "---", frequency_line,
                                              oled_result_display_status_text(result->status));
    }

    format_measurement_value(&value, result);
    rlc_text_append(&range, "RF ");
    append_resistance(&range, feedback_ohm);
    rlc_text_append(&range, result->calibrated ? " CAL" : " RAW");
    return oled_result_display_show_lines(component_text(result->component), value_line,
                                          frequency_line, range_line);
}
