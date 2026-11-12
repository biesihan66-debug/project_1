
#include <stdio.h>
#include <string.h>
#define main firmware_main
#include "main_after.c"
#undef main
static bool simulated_button;
static bool save_ok = true;
static RlcStatus calibration_status = RLC_STATUS_OK;
static RlcMeasurementPort test_port = {.feedback_ohm = {10, 100, 1000, 10000, 100000}};
bool rlc_board_initialize(void) { return true; }
const RlcMeasurementPort *rlc_board_measurement_port(void) { return &test_port; }
uint32_t rlc_board_millis(void) { return 100; }
bool rlc_board_button_pressed(void) { return simulated_button; }
void rlc_board_debug_write(const char *text) { puts(text); }
bool rlc_board_load_calibration(void *data, size_t size) { (void)data; (void)size; return false; }
bool rlc_board_save_calibration(const void *data, size_t size) {
    (void)data; (void)size; printf("SAVE %d\n", save_ok); return save_ok;
}
bool oled_result_display_initialize(void) { puts("DISPLAY INIT"); return true; }
bool oled_result_display_show_activity(const char *text) { puts(text); return true; }
bool oled_result_display_show_lines(const char *a, const char *b, const char *c, const char *d) {
    printf("PAGE %s | %s | %s | %s\n", a, b, c, d); return true;
}
bool oled_result_display_show_measurement(const RlcMeasurementResult *r, float feedback) {
    printf("DISPLAY RESULT %d %a\n", r->status, (double)feedback); return true;
}
const char *oled_result_display_status_text(RlcStatus status) { (void)status; return "ERROR"; }
void measurement_debug_output_result(const RlcMeasurementResult *r, uint32_t elapsed, bool ok) {
    printf("DEBUG %d %lu %d\n", r->status, (unsigned long)elapsed, ok);
}
void measurement_debug_output_calibration(bool open, RlcStatus status) {
    printf("CAL %d %d\n", open, status);
}
RlcMeasurementResult rlc_measure_auto(const RlcMeasurementPort *p, const RlcCalibration *cal,
                                      RlcMeasurementMode mode) {
    (void)p; (void)cal; printf("AUTO %d\n", mode);
    return (RlcMeasurementResult){.status=RLC_STATUS_OK, .range_index=2};
}
RlcMeasurementResult rlc_measure_fixed(const RlcMeasurementPort *p, const RlcCalibration *cal,
                                       uint8_t frequency, uint8_t range) {
    (void)p; (void)cal; printf("FIXED %u %u\n", frequency, range);
    return (RlcMeasurementResult){.status=RLC_STATUS_OK, .range_index=range};
}
RlcStatus rlc_capture_calibration(const RlcMeasurementPort *p, RlcCalibration *cal,
                                  bool open, RlcMeasurementMode mode) {
    (void)p; printf("CAPTURE %d %d\n", open, mode);
    if (calibration_status == RLC_STATUS_OK) {
        if (open) cal->open_mask = 1; else cal->short_mask = 1;
    }
    return calibration_status;
}
static void feed(uint32_t time, bool pressed) {
    simulated_button = pressed;
    const ButtonEvent event = read_button_event(time);
    printf("BUTTON %lu %d %d %d %d %d %lu %lu\n", (unsigned long)time, pressed, event,
           button.raw_pressed, button.stable_pressed, button.long_reported,
           (unsigned long)button.raw_changed_ms, (unsigned long)button.pressed_ms);
}
static void event(ButtonEvent button_event) {
    handle_button_event(button_event, &test_port, 10);
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
    event(BUTTON_SHORT_PRESS); event(BUTTON_LONG_PRESS); event(BUTTON_SHORT_PRESS);
    save_ok = false; event(BUTTON_SHORT_PRESS);
    save_ok = true; event(BUTTON_SHORT_PRESS);
    event(BUTTON_LONG_PRESS); calibration_status = RLC_STATUS_TIMEOUT;
    event(BUTTON_SHORT_PRESS); event(BUTTON_LONG_PRESS);
    display_ready = false; event(BUTTON_SHORT_PRESS);
    return 0;
}
