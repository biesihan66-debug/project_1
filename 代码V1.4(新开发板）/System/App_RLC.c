/*
 * 电桥应用流程。入口 App_Init()/App_Run() 由 USER/main.c 调用。
 * App_Run() 只做两件事：读取按键事件，然后处理测量或校准。
 * 短按：测量页启动测量；校准页采集当前步骤。长按：进入或退出校准。
 *
 * 应用状态与位置：
 * button 的按键状态已封装在 Key.c，应用只读取 KeyEvent。
 * app_page 记录测量/开路校准/短路校准页面；display_ready 记录 OLED 是否可用。
 * active_calibration 供测量读，pending_calibration 由开短路流程写，
 * 两者是本文件的静态变量。校准数组下标见 RLC_Calibration.h，
 * 配对成功并写入板级 Flash 后才更新 active_calibration。
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

#ifndef RLC_APP_FIXED_1KHZ
#define RLC_APP_FIXED_1KHZ 0
#endif

typedef enum
{
    APP_MEASURE,
    APP_CALIBRATE_OPEN,
    APP_CALIBRATE_SHORT
} AppPage;

/* 只有主循环访问这些状态，中断负责板级计时和 DMA。 */
static AppPage app_page = APP_MEASURE;
static RLC_Calibration active_calibration;
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

static void ShowReady(const char *hint)
{
#if RLC_APP_FIXED_1KHZ
    ShowPage("RLC METER", "PRESS TO MEASURE", "FIXED 1 KHZ", hint);
#else
    ShowPage("RLC METER", "PRESS TO MEASURE", "EXTENDED AUTO", hint);
#endif
}

static void ShowCalibrationPrompt(const char *hint)
{
    if (app_page == APP_CALIBRATE_OPEN)
    {
        ShowPage("CAL OPEN", "REMOVE COMPONENT", "PRESS WHEN READY", hint);
    }
    else
    {
        ShowPage("CAL SHORT", "SHORT THE CLIP", "PRESS WHEN READY", hint);
    }
}

static void RunMeasurement(uint32_t command_ms)
{
    RLC_Result result;
    float feedback_ohm = 0.0f;

    if (display_ready)
    {
        display_ready = OLED_ShowActivity("MEASURING");
    }
#if RLC_APP_FIXED_1KHZ
    result = RLC_MeasureFixed(&active_calibration, RLC_FREQUENCY_1_KHZ, RLC_RANGE_1_KOHM);
#else
    result = RLC_MeasureAuto(&active_calibration, RLC_MODE_EXTENDED);
#endif
    if (result.range_index < RLC_RANGE_COUNT)
    {
        feedback_ohm = bsp_feedback_ohm[result.range_index];
    }
    if (display_ready)
    {
        display_ready = OLED_ShowResult(&result, feedback_ohm);
    }
    Serial_PrintResult(&result, BSP_GetMillis() - command_ms, display_ready);
}

/* 开路与短路两步都成功才写入 Flash。失败时留在当前步骤，短按可重试。 */
static void CaptureCalibrationStep(void)
{
    const bool open = app_page == APP_CALIBRATE_OPEN;
    const char *title = open ? "CAL OPEN" : "CAL SHORT";
    RLC_Status status;

    ShowPage(title, "CAPTURING", "", "PLEASE WAIT");
    status = RLC_CaptureCalibration(&pending_calibration, open, RLC_MODE_EXTENDED);
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
    if (!Flash_SaveCalibration(&pending_calibration, sizeof pending_calibration))
    {
        ShowCalibrationPrompt("FLASH SAVE ERROR");
        Serial_PrintCalibration(false, RLC_STATUS_CALIBRATION_ERROR);
        return;
    }
    active_calibration = pending_calibration;
    app_page = APP_MEASURE;
    ShowReady("CAL SAVED");
    Serial_PrintCalibration(false, RLC_STATUS_OK);
}

static void HandleButtonEvent(KeyEvent event, uint32_t command_ms)
{
    EnsureDisplay();
    /* 短按执行当前页面的动作。 */
    if (event == KEY_SHORT_PRESS)
    {
        if (app_page == APP_MEASURE)
        {
            RunMeasurement(command_ms);
        }
        else
        {
            CaptureCalibrationStep();
        }
        return;
    }

    /* 长按只负责切换页面；固定台架模式不进入自动校准。 */
    if (RLC_APP_FIXED_1KHZ)
    {
        ShowReady("BENCH TEST");
    }
    else if (app_page == APP_MEASURE)
    {
        pending_calibration = (RLC_Calibration){0};
        app_page = APP_CALIBRATE_OPEN;
        ShowCalibrationPrompt("HOLD TO CANCEL");
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
    if (Flash_LoadCalibration(&pending_calibration, sizeof pending_calibration))
    {
        active_calibration = pending_calibration;
    }
    EnsureDisplay();
    ShowReady(RLC_APP_FIXED_1KHZ ? "BENCH TEST" : "HOLD FOR CAL");
    BSP_DebugWrite("RLC READY\r\n");
    return true;
}

/* 每轮调用一次；无按键事件时立即返回，测量时由各模块管理超时。 */
void App_Run(void)
{
    const uint32_t now_ms = BSP_GetMillis();
    const KeyEvent event = Key_GetEvent(now_ms);
    if (event != KEY_NONE)
    {
        HandleButtonEvent(event, now_ms);
    }
}
