/*
 * OLED 与串口共用的 ASCII 数字格式化。TextBuffer.buffer 指向调用者的字符数组，
 * capacity 含结尾零字节，length 是已写字符数，下标从 0 开始。
 * OLED 第 2 至 4 行使用工程量文本，串口以科学计数法输出带单位的字段。
 * 本模块无持久状态，也不调用浮点 printf。
 */
#ifndef TEXT_FORMAT_H
#define TEXT_FORMAT_H

#include <stddef.h>
#include <stdint.h>

typedef struct
{
    char *buffer;    /* 调用者提供的字符数组。 */
    size_t capacity; /* 数组总容量，包括末尾 '\0'。 */
    size_t length;   /* 已写字符数，不包括 '\0'。 */
} TextBuffer;

/* 缓冲区由调用者提供；各追加函数保证保留字符串结束符。实现位于 Text_Format.c。 */
TextBuffer Text_Begin(char *buffer, size_t capacity);
/* 以下函数只向尾部追加内容；*text 指向 Text_Begin() 返回的状态变量。 */
void Text_AddChar(TextBuffer *text, char value);
void Text_Append(TextBuffer *text, const char *value);
void Text_AddUnsigned(TextBuffer *text, uint32_t value);
/* 定点小数：例如 scaled=1234、divisor=1000 显示 1.234；divisor 必须为 10 的非负整数次幂。 */
void Text_AddDecimal(TextBuffer *text, uint32_t scaled, uint32_t divisor);
void Text_AddScientific(TextBuffer *text, float value);

#endif
