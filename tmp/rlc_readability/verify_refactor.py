from pathlib import Path
import hashlib
import subprocess

ROOT = Path('C:/Users/1/Desktop/集训电赛/测量代码V1.1')
WORK = Path(__file__).resolve().parent
SYSTEM = ROOT / 'System'


def compile_and_run(name, sources, defines=()):
    executable = WORK / f'{name}.exe'
    command = ['gcc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
               '-I' + str(SYSTEM), *defines, *map(str, sources), '-o', str(executable), '-lm']
    compiled = subprocess.run(command, capture_output=True)
    if compiled.returncode:
        print(compiled.stderr.decode('utf-8', errors='replace'))
        compiled.check_returncode()
    result = subprocess.run([str(executable)], check=True, capture_output=True)
    (WORK / f'{name}.trace').write_bytes(result.stdout)
    return result.stdout


test = (ROOT / 'Tests/test_rlc_measurement.c').read_text(encoding='utf-8-sig')
test = test.replace('    board->frequency_index = frequency;',
                    '    printf("SET %u %u %lu\\n", frequency, range, (unsigned long)deadline);\n'
                    '    board->frequency_index = frequency;')
test = test.replace('    ++((TestBoard *)context)->stop_count;',
                    '    puts("STOP");\n    ++((TestBoard *)context)->stop_count;')
test = test.replace('    const float error = fabsf(result.value_si - value_si) / value_si;',
    '    printf("RESULT %d %d %a %lu %u %d %lu %a %a %a %a %a %a %a %a\\n",\n'
    '           result.status, result.component, (double)result.value_si,\n'
    '           (unsigned long)result.frequency_hz, result.range_index, result.calibrated,\n'
    '           (unsigned long)result.elapsed_ms, (double)result.impedance_ohm.re,\n'
    '           (double)result.impedance_ohm.im, (double)result.phasors.dut_v.re,\n'
    '           (double)result.phasors.dut_v.im, (double)result.phasors.tia_v.re,\n'
    '           (double)result.phasors.tia_v.im, (double)result.phasors.dut_peak_codes,\n'
    '           (double)result.phasors.tia_peak_codes);\n'
    '    const float error = fabsf(result.value_si - value_si) / value_si;')
instrumented = WORK / 'measurement_trace.c'
instrumented.write_text(test, encoding='utf-8')
math_sources = [SYSTEM / 'rlc_impedance_calculation.c', SYSTEM / 'open_short_calibration.c']
old = compile_and_run('measurement_before', [instrumented, WORK / 'automatic_before.c', *math_sources])
new = compile_and_run('measurement_after', [instrumented, SYSTEM / 'automatic_rlc_measurement.c', *math_sources])
assert old == new, 'Measurement result or setting sequence changed'
print(f'PASS: measurement traces identical ({len(new.splitlines())} records, sha256={hashlib.sha256(new).hexdigest()[:16]})')

app_test = r'''
#include <stdio.h>
#include <string.h>
#define main firmware_main
#include "APP_SOURCE"
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
'''
(WORK / 'main_after.c').write_bytes((ROOT / 'USER/main.c').read_bytes())
for mode, defines in [('Full', []), ('Basic', ['-DRLC_APP_BASIC_ONLY=1']),
                      ('Fixed1k', ['-DRLC_APP_FIXED_1KHZ=1'])]:
    traces = []
    for version, source in [('before', WORK / 'main_before.c'), ('after', WORK / 'main_after.c')]:
        harness = WORK / f'app_{mode}_{version}.c'
        harness.write_text(app_test.replace('APP_SOURCE', source.name), encoding='utf-8')
        traces.append(compile_and_run(f'app_{mode}_{version}', [harness], defines))
    assert traces[0] == traces[1], f'Button or application behavior changed in {mode}'
    print(f'PASS: {mode} button and calibration page traces identical ({len(traces[0].splitlines())} records)')
