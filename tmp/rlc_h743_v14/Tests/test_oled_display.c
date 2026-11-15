/* 运行实际 OLED 绘制代码，用 I2C 替身记录页地址、字模及传输失败。 */
#include "OLED.h"
#include "OLED_Display.h"
#include "OLED_I2C.h"
#include "delay.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t ticks_ms;
static uint8_t page;
static uint8_t written_pages;
static uint8_t pixels[8][128];
static unsigned transfer_count;
static bool reject_next_transfer;

uint32_t GetTick(void) { return ticks_ms; }
void Delay(uint32_t ms) { ticks_ms += ms; }

bool OLED_I2C_Write(uint8_t control, const uint8_t *data, uint16_t count)
{
    ++transfer_count;
    if (reject_next_transfer)
    {
        reject_next_transfer = false;
        return false;
    }
    if (control == 0x00u && count == 1u && (data[0] & 0xf8u) == 0xb0u)
    {
        page = data[0] & 7u;
    }
    if (control == 0x40u)
    {
        assert(count == sizeof pixels[page]);
        written_pages |= (uint8_t)(1u << page);
        memcpy(pixels[page], data, count);
    }
    return true;
}

static void check_line(uint8_t display_page, const char *text)
{
    uint8_t expected[128] = {0};

    for (size_t i = 0u; text[i] != '\0'; ++i)
    {
        assert((i + 1u) * OLED_6X8 <= sizeof expected);
        memcpy(&expected[i * OLED_6X8], OLED_F6x8[(unsigned char)text[i] - ' '], OLED_6X8);
    }
    assert(memcmp(pixels[display_page], expected, sizeof expected) == 0);
}

int main(void)
{
    RLC_Result result = {.status = RLC_STATUS_OK, .component = RLC_COMPONENT_C,
                         .component_value = 47e-12f, .frequency_hz = 100000u, .calibrated = true};

    assert(OLED_DisplayInit());
    assert(ticks_ms == 100u && written_pages == 0xffu);
    written_pages = 0u;
    assert(OLED_ShowResult(&result, 1000.0f));
    assert(written_pages == 0x55u);
    check_line(0u, "C  CAPACITANCE");
    check_line(2u, "47 pF");
    check_line(4u, "100 kHz");
    check_line(6u, "RF 1 kOHM CAL");

    written_pages = 0u;
    assert(OLED_ShowActivity("WAIT"));
    assert(written_pages == 1u);
    check_line(0u, "WAIT");
    check_line(2u, "47 pF");

    unsigned before = transfer_count;
    reject_next_transfer = true;
    assert(!OLED_ShowActivity("MEASURING"));
    assert(transfer_count == before + 1u);
    reject_next_transfer = true;
    assert(!OLED_DisplayInit());
    assert(OLED_DisplayInit());

    result.status = RLC_STATUS_TIMEOUT;
    assert(OLED_ShowResult(&result, 1000.0f));
    check_line(0u, "MEASURE ERROR");
    check_line(2u, "---");
    check_line(4u, "100 kHz");
    check_line(6u, "TIMEOUT");
    puts("PASS: reference OLED glyphs, row updates, clearing, and I2C failure recovery");
    return 0;
}
