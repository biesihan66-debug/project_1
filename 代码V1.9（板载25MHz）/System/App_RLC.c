/*
 * 电桥应用流程。入口 App_Init()/App_Run() 由 USER/main.c 调用。
 * App_Run() 只做两件事：读取按键事件，然后处理测量或校准。
 * 短按：测量页启动测量；校准页采集当前步骤。长按：进入或退出校准。
 *
 * 应用状态与位置：
 * button 的按键状态已封装在 Key.c，应用只读取 KeyEvent。
 * app_page 记录测量/开路校准/短路校准页面；display_ready 记录 OLED 是否可用。
 * stored_calibration[0/1] 分别存两线和四线的已保存记录，供测量读取。
 * pending_calibration 存本轮开短校准，保存失败或取消时继续使用原记录。
 * 两者是本文件的静态变量。频率、量程下标见 RLC_Calibration.h。
 * RunMeasurement() 的 result 在本次调用栈上，类型/数值/频率/量程分别
 * 送到 OLED 第 1/2/3/4 行，I/Q、复阻抗和用时经 USART 输出。
 * RLC_APP_FIXED_1KHZ 用于 1 kHz / 1 kΩ 固定台架；Full 使用含 100 kHz 的自动测量。
 */
#include "App_RLC.h"
#include "Key.h"
#include "RLC_Measure.h"
#include "Serial_Debug.h"
#include "OLED_Display.h"
#include "BSP_Board.h"
#include "Flash_Calibration.h"
#include <string.h>

#ifndef RLC_APP_FIXED_1KHZ
#define RLC_APP_FIXED_1KHZ 0
#endif

typedef enum
{
    APP_SELECT_WIRING,
    APP_SELECT_BAND,
    APP_MEASURE,
    APP_MENU,
    APP_LIMITED_TEST,
    APP_CALIBRATE_OPEN,
    APP_CALIBRATE_SHORT
} AppPage;

/* 只有主循环访问这些状态，中断负责板级计时和 DMA。 */
static AppPage app_page = APP_SELECT_WIRING;
static bool selected_four_wire = true;
static RLC_Mode selected_band = RLC_MODE_BASIC;
static uint8_t menu_index;
static RLC_Calibration stored_calibration[2];
static RLC_Calibration pending_calibration;
static bool display_ready;

/* 总线事务失败后，下次按键时可重新初始化 OLED。 */
static void EnsureDisplay(void)
{
    if (!display_ready)
    {
        display_ready = OLED_DisplayInit();
    }
}

static void ShowPage(const char *line1, const char *line2, const char *line3, const char *line4)
{
    if (display_ready)
    {
        display_ready = OLED_ShowLines(line1, line2, line3, line4);
    }
}

static const char *WiringLabel(void)
{
    return selected_four_wire ? "4 WIRE JP1 2-3" : "2 WIRE JP1 1-2";
}

static const char *BandLabel(void)
{
    return selected_band == RLC_MODE_BASIC ? "BASIC 100-10K" : "EXT UP TO 100K";
}

static void ShowReady(const char *hint)
{
    ShowPage("RLC V1.9", WiringLabel(), BandLabel(), hint);
}

static void ShowSetup(void)
{
    if (app_page == APP_SELECT_WIRING)
    {
        ShowPage("CONFIRM JP1", WiringLabel(), "PRESS TO CHANGE", "HOLD TO SELECT");
    }
    else if (app_page == APP_SELECT_BAND)
    {
        ShowPage("SELECT BAND", BandLabel(), "PRESS TO CHANGE", "HOLD TO SELECT");
    }
    else
    {
        const char *const entries[] = {"OPEN SHORT CAL", "CHANGE 2W / 4W", "CHANGE BAND", "1K LIMITED TEST"};
        ShowPage("SETTINGS", entries[menu_index], "PRESS NEXT", "HOLD TO OPEN");
    }
}

static void ShowCalibrationPrompt(const char *hint)
{
    if (app_page == APP_CALIBRATE_OPEN)
    {
        ShowPage("CAL OPEN", selected_four_wire ? "OPEN STANDARD" : "REMOVE COMPONENT",
                 "PRESS WHEN READY", hint);
    }
    else
    {
        ShowPage("CAL SHORT", "SHORT THE CLIP", "PRESS WHEN READY", hint);
    }
}

static void RunMeasurement(uint32_t command_ms, bool limited)
{
    RLC_Result result;
    float feedback_ohm = 0.0f;

    if (display_ready)
    {
        display_ready = OLED_ShowActivity("MEASURING");
    }
    const RLC_Calibration *cal = &stored_calibration[selected_four_wire ? 1u : 0u];
    if (limited || RLC_APP_FIXED_1KHZ)
    {
        result = RLC_MeasureFixed(limited ? NULL : cal, RLC_FREQUENCY_1_KHZ, RLC_RANGE_1_KOHM);
    }
    else
    {
        result = RLC_MeasureAuto(cal, selected_band);
    }
    if (result.range_index < RLC_RANGE_COUNT)
    {
        feedback_ohm = bsp_feedback_ohm[result.range_index];
    }
    if (display_ready)
    {
        display_ready = OLED_ShowResult(&result, feedback_ohm);
    }
    const uint32_t finished = BSP_GetMillis();
    BSP_DebugWrite(limited || RLC_APP_FIXED_1KHZ ? "RUN LIMITED 1K\r\n" : "RUN AUTO\r\n");
    Serial_PrintResult(&result, finished - command_ms, finished - Key_PressedAt(), display_ready);
}

/* 开路与短路两步都成功才写入 Flash。失败时留在当前步骤，短按可重试。 */
static void CaptureCalibrationStep(void)
{
    const bool open = app_page == APP_CALIBRATE_OPEN;
    const char *title = open ? "CAL OPEN" : "CAL SHORT";
    RLC_Status status;

    ShowPage(title, "CAPTURING", "", "PLEASE WAIT");
    status = RLC_CaptureCalibration(&pending_calibration, open, selected_band);
    if (status != RLC_STATUS_OK)
    {
        ShowCalibrationPrompt(OLED_GetStatusText(status));
        Serial_PrintCalibration(open, status);
        return;
    }
    if (open)
    {
        app_page = APP_CALIBRATE_SHORT;
        ShowCalibrationPrompt("HOLD TO CANCEL");
        Serial_PrintCalibration(true, RLC_STATUS_OK);
        return;
    }
    const uint8_t wiring = selected_four_wire ? 1u : 0u;
    RLC_Calibration previous = stored_calibration[wiring];
    stored_calibration[wiring] = pending_calibration;
    if (!Flash_SaveCalibration(stored_calibration, sizeof stored_calibration))
    {
        stored_calibration[wiring] = previous;
        ShowCalibrationPrompt("FLASH SAVE ERROR");
        Serial_PrintCalibration(false, RLC_STATUS_CALIBRATION_ERROR);
        return;
    }
    app_page = APP_MEASURE;
    ShowReady("CAL SAVED");
    Serial_PrintCalibration(false, RLC_STATUS_OK);
}

static void HandleButtonEvent(KeyEvent event, uint32_t command_ms)
{
    EnsureDisplay();
    if (app_page == APP_SELECT_WIRING)
    {
        if (event == KEY_SHORT_PRESS)
        {
            selected_four_wire = !selected_four_wire;
        }
        else
        {
            RLC_SelectSenseProfile(selected_four_wire);
            BSP_DebugWrite(selected_four_wire ? "WIRING 4W\r\n" : "WIRING 2W\r\n");
            app_page = APP_SELECT_BAND;
        }
        ShowSetup();
    }
    else if (app_page == APP_SELECT_BAND)
    {
        if (event == KEY_SHORT_PRESS)
        {
            selected_band = selected_band == RLC_MODE_BASIC ? RLC_MODE_EXTENDED : RLC_MODE_BASIC;
            ShowSetup();
        }
        else
        {
            app_page = APP_MEASURE;
            BSP_DebugWrite(selected_band == RLC_MODE_BASIC ? "BAND BASIC\r\n" : "BAND EXT\r\n");
            ShowReady("PRESS TO MEASURE");
        }
    }
    else if (app_page == APP_MENU)
    {
        if (event == KEY_SHORT_PRESS)
        {
            menu_index = (uint8_t)((menu_index + 1u) % 4u);
            ShowSetup();
        }
        else if (menu_index == 0u)
        {
            /* 本轮只登记实际校过的频点，避免 BASIC 保存后混入旧夹具的 100 kHz 校准。 */
            memset(&pending_calibration, 0, sizeof pending_calibration);
            app_page = APP_CALIBRATE_OPEN;
            ShowCalibrationPrompt("HOLD TO CANCEL");
        }
        else if (menu_index == 3u)
        {
            app_page = APP_LIMITED_TEST;
            ShowPage("1K LIMITED TEST", WiringLabel(), "PRESS RUN 1K", "HOLD TO EXIT");
        }
        else
        {
            app_page = menu_index == 1u ? APP_SELECT_WIRING : APP_SELECT_BAND;
            ShowSetup();
        }
    }
    else if (app_page == APP_LIMITED_TEST)
    {
        if (event == KEY_SHORT_PRESS)
        {
            RunMeasurement(command_ms, true);
        }
        else
        {
            app_page = APP_MEASURE;
            ShowReady("PRESS TO MEASURE");
        }
    }
    else if (app_page == APP_MEASURE)
    {
        if (event == KEY_SHORT_PRESS)
        {
            RunMeasurement(command_ms, false);
        }
        else
        {
            menu_index = 0u;
            app_page = APP_MENU;
            ShowSetup();
        }
    }
    else if (event == KEY_SHORT_PRESS)
    {
        CaptureCalibrationStep();
    }
    else
    {
        app_page = APP_MEASURE;
        ShowReady("CAL CANCELLED");
    }
}

/* 上电初始化：硬件 -> 按键 -> 加载校准 -> 显示待机页面。 */
bool App_Init(void)
{
    if (!BSP_Init())
    {
        BSP_DebugWrite("BOARD INIT ERROR\r\n");
        return false;
    }
    Key_Init();
    (void)Flash_LoadCalibration(stored_calibration, sizeof stored_calibration);
    EnsureDisplay();
    ShowSetup();
    BSP_DebugWrite(RLC_APP_FIXED_1KHZ ? "RLC READY V1.9 FIXED1K 25MHZ\r\n" :
                                      "RLC READY V1.9 FULL 25MHZ\r\n");
    return true;
}

/* 每轮先处理待重试的停波，再读取按键。测量和校准各自管理截止时刻。 */
void App_Run(void)
{
    BSP_RetryPendingStop();
    const uint32_t now_ms = BSP_GetMillis();
    const KeyEvent event = Key_GetEvent(now_ms);
    if (event != KEY_NONE)
    {
        HandleButtonEvent(event, now_ms);
    }
}
