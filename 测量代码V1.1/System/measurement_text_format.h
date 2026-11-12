/*
 * OLED 与串口共用的 ASCII 数字格式化。RlcText.buffer 指向调用者的字符数组，
 * capacity 含结尾零字节，length 是已写字符数，下标从 0 开始。
 * OLED 第 2 至 4 行使用工程量文本，串口以科学计数法输出带单位的字段。
 * 本模块无持久状态，也不调用浮点 printf。
 */
#ifndef MEASUREMENT_TEXT_FORMAT_H
#define MEASUREMENT_TEXT_FORMAT_H

#include <float.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
} RlcText;

/* capacity 由调用者传入，写到缓冲区末尾后保留字符串结束符。 */
static inline RlcText rlc_text_begin(char *buffer, size_t capacity)
{
    RlcText text = {buffer, capacity, 0u};
    if (capacity != 0u) {
        buffer[0] = '\0';
    }
    return text;
}

static inline void rlc_text_char(RlcText *text, char value)
{
    if (text->length + 1u < text->capacity) {
        text->buffer[text->length++] = value;
        text->buffer[text->length] = '\0';
    }
}

static inline void rlc_text_append(RlcText *text, const char *value)
{
    while (*value != '\0') {
        rlc_text_char(text, *value++);
    }
}

/* digits[] 暂存十进制数字，低位先写，最多 10 位。 */
static inline void rlc_text_unsigned(RlcText *text, uint32_t value)
{
    char digits[10];
    size_t count = 0u;

    do {
        digits[count++] = (char)('0' + value % 10u);
        value /= 10u;
    } while (value != 0u);

    while (count != 0u) {
        rlc_text_char(text, digits[--count]);
    }
}

/* scaled 已按 10^n 放大，divisor 是 10^n。小数末尾的零可以省略。 */
static inline void rlc_text_decimal(RlcText *text, uint32_t scaled, uint32_t divisor)
{
    uint32_t remainder = scaled % divisor;
    rlc_text_unsigned(text, scaled / divisor);

    if (remainder != 0u) {
        rlc_text_char(text, '.');
        while (remainder != 0u) {
            divisor /= 10u;
            rlc_text_char(text, (char)('0' + remainder / divisor));
            remainder %= divisor;
        }
    }
}

/* 接收任意有限单精度值。用于串口输出，单位由字段名指定。 */
static inline void rlc_text_scientific(RlcText *text, float value)
{
    int exponent = 0;
    uint32_t scaled;

    if (!(value >= -FLT_MAX && value <= FLT_MAX)) {
        rlc_text_append(text, "INVALID");
        return;
    }
    if (value == 0.0f) {
        rlc_text_char(text, '0');
        return;
    }
    if (value < 0.0f) {
        rlc_text_char(text, '-');
        value = -value;
    }
    while (value >= 10.0f) {
        value *= 0.1f;
        ++exponent;
    }
    while (value < 1.0f) {
        value *= 10.0f;
        --exponent;
    }
    scaled = (uint32_t)(value * 100000.0f + 0.5f);
    if (scaled >= 1000000u) {
        scaled = 100000u;
        ++exponent;
    }
    rlc_text_decimal(text, scaled, 100000u);
    if (exponent != 0) {
        rlc_text_char(text, 'E');
        if (exponent < 0) {
            rlc_text_char(text, '-');
            exponent = -exponent;
        }
        rlc_text_unsigned(text, (uint32_t)exponent);
    }
}

#endif
