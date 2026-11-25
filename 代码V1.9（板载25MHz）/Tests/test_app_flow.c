/* Exercise the production Full menu with simulated devices and key events. */
#include "App_RLC.h"
#include "BSP_Board.h"
#include "Key.h"
#include "RLC_Measure.h"
#include "Flash_Calibration.h"
#include "OLED_Display.h"
#include "Serial_Debug.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifndef RLC_APP_FIXED_1KHZ
#define RLC_APP_FIXED_1KHZ 0
#endif

static KeyEvent event;
static unsigned fixed_runs, auto_runs, open_runs, short_runs, writes;
static unsigned stop_checks;
static uint32_t ticks, pressed_at;
static bool four_wire;
static bool fail_save;
static RLC_Mode expected_band = RLC_MODE_EXTENDED;
static RLC_Calibration saved_profiles[2];
/* 每个频点有五档。BASIC 校前三个频点，EXT 校四个。 */
static const uint32_t basic_mask = (UINT32_C(1) << (RLC_FREQUENCY_100_KHZ * RLC_RANGE_COUNT)) - 1u;
static const uint32_t ext_mask = (UINT32_C(1) << (RLC_FREQUENCY_COUNT * RLC_RANGE_COUNT)) - 1u;
static char title[32], detail[32];
const float bsp_feedback_ohm[RLC_RANGE_COUNT] = {10, 100, 1000, 10000, 100000};

static void press(KeyEvent key)
{
    pressed_at = ticks - 70u;
    event = key;
    App_Run();
}

/* 这两个操作从待机页开始，按与实物相同的顺序进入菜单。 */
static void open_calibration(void)
{
    press(KEY_LONG_PRESS);
    assert(strcmp(detail, "OPEN SHORT CAL") == 0);
    press(KEY_LONG_PRESS);
}

static void switch_band(void)
{
    press(KEY_LONG_PRESS);
    press(KEY_SHORT_PRESS);
    assert(strcmp(detail, RLC_APP_FIXED_1KHZ ? "CHANGE CAL BAND" : "CHANGE BAND") == 0);
    press(KEY_LONG_PRESS);
    press(KEY_SHORT_PRESS);
    press(KEY_LONG_PRESS);
}

bool BSP_Init(void) { return true; }
void BSP_RetryPendingStop(void) { ++stop_checks; }
void BSP_DebugWrite(const char *text) { (void)text; }
uint32_t BSP_GetMillis(void) { return ticks; }
void Key_Init(void) {}
KeyEvent Key_GetEvent(uint32_t now) { (void)now; return event; }
uint32_t Key_PressedAt(void) { return pressed_at; }
bool OLED_DisplayInit(void) { return true; }
bool OLED_ShowLines(const char *a, const char *b, const char *c, const char *d)
{
    (void)c; (void)d;
    snprintf(title, sizeof title, "%s", a);
    snprintf(detail, sizeof detail, "%s", b);
    return true;
}
bool OLED_ShowActivity(const char *msg) { (void)msg; return true; }
bool OLED_ShowResult(const RLC_Result *r, float rf)
{
    (void)r; (void)rf;
    ticks += 12;
    return true;
}
const char *OLED_GetStatusText(RLC_Status status) { (void)status; return "ERROR"; }
void Serial_PrintResult(const RLC_Result *r, uint32_t display, uint32_t press_time, bool ok)
{
    assert(r->status == RLC_STATUS_OK && ok && display == 12u && press_time == 82u);
}
void Serial_PrintCalibration(bool open, RLC_Status status)
{
    assert(status == (!open && fail_save ? RLC_STATUS_CALIBRATION_ERROR : RLC_STATUS_OK));
}
bool Flash_LoadCalibration(void *p, size_t n)
{
    assert(n == sizeof saved_profiles);
    memcpy(p, saved_profiles, n);
    return true;
}
bool Flash_SaveCalibration(const void *p, size_t n)
{
    assert(n == sizeof saved_profiles);
    if (fail_save) return false;
    memcpy(saved_profiles, p, n);
    ++writes;
    return true;
}
void RLC_SelectSenseProfile(bool four) { four_wire = four; }
RLC_Result RLC_MeasureFixed(const RLC_Calibration *cal, uint8_t f, uint8_t r)
{
    assert(f == RLC_FREQUENCY_1_KHZ && r == RLC_RANGE_1_KOHM);
    if (fixed_runs < 2u)
        assert(cal == NULL); /* Explicit limited test, always RAW. */
    else
        assert(RLC_APP_FIXED_1KHZ && four_wire && cal &&
               memcmp(cal, &saved_profiles[1], sizeof *cal) == 0);
    ++fixed_runs;
    return (RLC_Result){.status = RLC_STATUS_OK};
}
RLC_Result RLC_MeasureAuto(const RLC_Calibration *cal, RLC_Mode mode)
{
    assert(cal && four_wire && mode == expected_band);
    assert(memcmp(cal, &saved_profiles[1], sizeof *cal) == 0);
    ++auto_runs;
    return (RLC_Result){.status = RLC_STATUS_OK};
}
RLC_Status RLC_CaptureCalibration(RLC_Calibration *cal, bool open, RLC_Mode mode)
{
    const uint32_t mask = mode == RLC_MODE_BASIC ? basic_mask : ext_mask;
    assert(four_wire && mode == expected_band);
    if (open)
    {
        ++open_runs;
        cal->open_mask |= mask;
        cal->ideal_open_mask |= mask;
    }
    else
    {
        ++short_runs;
        cal->short_mask |= mask;
    }
    return RLC_STATUS_OK;
}

int main(void)
{
    saved_profiles[0].open_mask = saved_profiles[0].short_mask = 1u;
    saved_profiles[0].short_ohm[0][0].real = 0.15f;
    const RLC_Calibration two_wire = saved_profiles[0];
    assert(App_Init() && four_wire && strcmp(title, RLC_APP_FIXED_1KHZ ? "SELECT CAL BAND" : "SELECT BAND") == 0);
    App_Run(); /* 即使没有按键，主循环也应处理尚未送达的停波指令。 */
    assert(stop_checks == 1u);
    press(KEY_LONG_PRESS);
    press(KEY_LONG_PRESS);
    for (unsigned i = 0; i < 2; ++i) press(KEY_SHORT_PRESS);
    assert(strcmp(detail, "1K LIMITED TEST") == 0);
    press(KEY_LONG_PRESS);
    press(KEY_SHORT_PRESS);
    press(KEY_SHORT_PRESS);
    assert(fixed_runs == 2u && auto_runs == 0u && writes == 0u);
    press(KEY_LONG_PRESS); /* exit bench -> ready */
    switch_band(); /* BASIC -> EXT */
    press(KEY_SHORT_PRESS);
    assert(RLC_APP_FIXED_1KHZ ? (fixed_runs == 3u && auto_runs == 0u) : auto_runs == 1u);
    open_calibration();
    assert(strcmp(detail,"OPEN STANDARD") == 0);
    press(KEY_SHORT_PRESS);
    assert(open_runs == 1u && writes == 0u && strcmp(title,"CAL SHORT") == 0);
    press(KEY_SHORT_PRESS);
    assert(short_runs == 1u && writes == 1u);
    assert(saved_profiles[1].open_mask == ext_mask && saved_profiles[1].short_mask == ext_mask);

    switch_band();
    expected_band = RLC_MODE_BASIC;
    open_calibration();     /* 校完开路取消，保留上次 EXT 记录。 */
    press(KEY_SHORT_PRESS);
    press(KEY_LONG_PRESS);
    press(KEY_SHORT_PRESS); /* 实际应用仍把旧记录交给测量。 */
    assert(writes == 1u && saved_profiles[1].open_mask == ext_mask);

    open_calibration();
    press(KEY_SHORT_PRESS);
    fail_save = true;
    press(KEY_SHORT_PRESS);
    assert(writes == 1u && strcmp(title, "CAL SHORT") == 0);
    assert(saved_profiles[1].open_mask == ext_mask && saved_profiles[1].short_mask == ext_mask);
    press(KEY_LONG_PRESS);  /* 写入失败后取消，RAM 和 Flash 应保持一致。 */
    press(KEY_SHORT_PRESS);
    fail_save = false;

    open_calibration();
    press(KEY_SHORT_PRESS);
    press(KEY_SHORT_PRESS);
    assert(writes == 2u);
    assert(saved_profiles[1].open_mask == basic_mask && saved_profiles[1].short_mask == basic_mask);
    assert(saved_profiles[1].ideal_open_mask == basic_mask);
    assert(memcmp(&saved_profiles[0], &two_wire, sizeof two_wire) == 0);
    switch_band();          /* 回 EXT，确认实际测量收到刚保存的 BASIC 记录。 */
    expected_band = RLC_MODE_EXTENDED;
    press(KEY_SHORT_PRESS);
    puts(RLC_APP_FIXED_1KHZ ? "PASS: Fixed1k menus, fixed frequency/range, calibration and retries" :
                            "PASS: Full menus, limited-only repeats, OLED timing, fresh calibration, cancel and failed saves");
    return 0;
}
