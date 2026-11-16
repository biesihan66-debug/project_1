#include "Flash_Calibration.h"

#include <stdio.h>
#include <string.h>
#define main firmware_main
#include "style_app_after.c"
#undef main
static bool simulated_button;
static bool save_ok = true;
static RLC_Status calibration_status = RLC_STATUS_OK;
static RLC_Hardware test_port = {.feedback_ohm = {10, 100, 1000, 10000, 100000}};
bool BSP_Init(void) { return true; }
const RLC_Hardware *BSP_GetMeasureHardware(void) { return &test_port; }
uint32_t BSP_GetMillis(void) { return 100; }
bool BSP_KeyIsPressed(void) { return simulated_button; }
void BSP_DebugWrite(const char *text) { puts(text); }
bool Flash_LoadCalibration(void *data, size_t size) { (void)data; (void)size; return false; }
bool Flash_SaveCalibration(const void *data, size_t size) {
    (void)data; (void)size; printf("SAVE %d\n", save_ok); return save_ok;
}
bool OLED_Init(void) { puts("DISPLAY INIT"); return true; }
bool OLED_ShowActivity(const char *text) { puts(text); return true; }
bool OLED_ShowLines(const char *a, const char *b, const char *c, const char *d) {
    printf("PAGE %s | %s | %s | %s\n", a, b, c, d); return true;
}
bool OLED_ShowResult(const RLC_Result *r, float feedback) {
    printf("DISPLAY RESULT %d %a\n", r->status, (double)feedback); return true;
}
const char *OLED_GetStatusText(RLC_Status status) { (void)status; return "ERROR"; }
void Serial_PrintResult(const RLC_Result *r, uint32_t elapsed, bool ok) {
    printf("DEBUG %d %lu %d\n", r->status, (unsigned long)elapsed, ok);
}
void Serial_PrintCalibration(bool open, RLC_Status status) {
    printf("CAL %d %d\n", open, status);
}
RLC_Result RLC_MeasureAuto(const RLC_Hardware *p, const RLC_Calibration *cal,
                                      RLC_Mode mode) {
    (void)p; (void)cal; printf("AUTO %d\n", mode);
    return (RLC_Result){.status=RLC_STATUS_OK, .range_index=2};
}
RLC_Result RLC_MeasureFixed(const RLC_Hardware *p, const RLC_Calibration *cal,
                                       uint8_t frequency, uint8_t range) {
    (void)p; (void)cal; printf("FIXED %u %u\n", frequency, range);
    return (RLC_Result){.status=RLC_STATUS_OK, .range_index=range};
}
RLC_Status RLC_CaptureCalibration(const RLC_Hardware *p, RLC_Calibration *cal,
                                  bool open, RLC_Mode mode) {
    (void)p; printf("CAPTURE %d %d\n", open, mode);
    if (calibration_status == RLC_STATUS_OK) {
        if (open) cal->open_mask = 1; else cal->short_mask = 1;
    }
    return calibration_status;
}
static void feed(uint32_t time, bool pressed) {
    simulated_button = pressed;
    const KeyEvent event = Key_GetEvent(time);
    printf("BUTTON %lu %d %d %d %d %d %lu %lu\n", (unsigned long)time, pressed, event,
           button.raw_pressed, button.stable_pressed, button.long_reported,
           (unsigned long)button.raw_changed_ms, (unsigned long)button.pressed_ms);
}
static void event(KeyEvent button_event) {
    HandleButtonEvent(button_event, &test_port, 10);
    printf("STATE %d %lu %lu %lu %lu\n", app_page,
           (unsigned long)active_calibration.open_mask, (unsigned long)active_calibration.short_mask,
           (unsigned long)pending_calibration.open_mask, (unsigned long)pending_calibration.short_mask);
}
int main(void) {
    feed(0, false); feed(10, true); feed(29, true); feed(30, true);
    feed(100, false); feed(119, false); feed(120, false);
    feed(1000, true); feed(1020, true); feed(1899, true); feed(1900, true);
    feed(1901, true); feed(2000, false); feed(2020, false);
    feed(3000, true); feed(3020, true); feed(4000, false); feed(4020, false);
    button = (ButtonState){0};
    uint32_t seed = 1234567u, now = UINT32_MAX - 10000u;
    bool held = false;
    for (unsigned i = 0; i < 6000; ++i) {
        seed = seed * 1664525u + 1013904223u;
        now += seed % 70u;
        if ((seed >> 16) % 19u == 0) held = !held;
        feed(now, held);
    }
    event(KEY_SHORT_PRESS); event(KEY_LONG_PRESS); event(KEY_SHORT_PRESS);
    save_ok = false; event(KEY_SHORT_PRESS);
    save_ok = true; event(KEY_SHORT_PRESS);
    event(KEY_LONG_PRESS); calibration_status = RLC_STATUS_TIMEOUT;
    event(KEY_SHORT_PRESS); event(KEY_LONG_PRESS);
    display_ready = false; event(KEY_SHORT_PRESS);
    return 0;
}
