/*
 * 按键去抖与长短按识别。主循环调用 Key_GetEvent()，本模块只处理按键。
 * button 是本文件的静态状态；raw_changed_ms/pressed_ms 单位 ms，由 SysTick 提供。
 * raw_pressed 是原始电平，stable_pressed 是去抖后的状态；long_reported 防止重复触发。
 * 不直接写 OLED；短按/长按事件交给 App_RLC.c，最终由应用决定 1~4 行内容。
 */
#include "Key.h"
#include "BSP_Board.h"

enum
{
    KEY_DEBOUNCE_MS = 20u,
    KEY_LONG_PRESS_MS = 900u
};

typedef struct
{
    bool raw_pressed;        /* 本次直接读到的按键状态。 */
    bool stable_pressed;     /* 持续稳定至少 20 ms 后接受的状态。 */
    bool long_reported;      /* 本轮按住期间是否已经发过长按事件。 */
    uint32_t raw_changed_ms; /* 原始电平最近一次改变的时刻。 */
    uint32_t pressed_ms;     /* 本轮有效按下的起始时刻。 */
} ButtonState;

static ButtonState button;

void Key_Init(void)
{
    button = (ButtonState){0};
}

/* 短按在去抖后的释放时触发，长按达到阈值时触发且只报告一次。 */
KeyEvent Key_GetEvent(uint32_t now_ms)
{
    const bool raw_pressed = BSP_KeyIsPressed();
    /* 1. 电平变化时重新开始去抖计时。 */
    if (button.raw_pressed != raw_pressed)
    {
        button.raw_pressed = raw_pressed;
        button.raw_changed_ms = now_ms;
    }

    /* 2. 接受稳定电平；释放时只上报尚未触发过的短按或长按。 */
    if (raw_pressed != button.stable_pressed && now_ms - button.raw_changed_ms >= KEY_DEBOUNCE_MS)
    {
        button.stable_pressed = raw_pressed;
        if (raw_pressed)
        {
            button.pressed_ms = button.raw_changed_ms;
            button.long_reported = false;
        }
        else
        {
            if (button.long_reported)
            {
                return KEY_NONE;
            }
            if (button.raw_changed_ms - button.pressed_ms >= KEY_LONG_PRESS_MS)
            {
                return KEY_LONG_PRESS;
            }
            return KEY_SHORT_PRESS;
        }
    }
    /* 3. 持续按住超过 900 ms，立即上报一次长按，无需等到松手。 */
    if (raw_pressed && button.stable_pressed && !button.long_reported &&
        now_ms - button.pressed_ms >= KEY_LONG_PRESS_MS)
    {
        button.long_reported = true;
        return KEY_LONG_PRESS;
    }
    return KEY_NONE;
}

/*
 * 电桥应用流程。入口 App_Init()/App_Run() 由 USER/main.c 调用。
 * App_Run() 只做两件事：读取按键事件，然后处理测量或校准。
 * 短按：测量页启动测量；校准页采集当前步骤。长按：进入或退出校准。
 *
 * 应用状态与位置：
 * button 的按键状态已封装在 Key.c，应用只读取 KeyEvent。
 * app_page 记录测量/开路校准/短路校准页面；display_ready 记录 OLED 是否可用。
 * measurement_mode 按 Keil 构建选项确定基础/扩展模式，不在运行中变化。
 * active_calibration 供测量读，pending_calibration 由开短路流程写，
 * 两者是本文件的静态变量。校准数组下标见 RLC_Calibration.h，
 * 配对成功并写入板级 Flash 后才更新 active_calibration。
 * RunMeasurement() 的 result 在本次调用栈上，类型/数值/频率/量程分别
 * 送到 OLED 第 1/2/3/4 行，I/Q、复阻抗和用时经 USART 输出。
 * RLC_APP_FIXED_1KHZ 用于 1 kHz / 1 kΩ 固定台架，RLC_APP_BASIC_ONLY
 * 用于基础三频验证，常规构建启用含 100 kHz 的自动测量。
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

#ifndef RLC_APP_BASIC_ONLY
#define RLC_APP_BASIC_ONLY 0
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
static const RLC_Mode measurement_mode = RLC_APP_BASIC_ONLY ? RLC_MODE_BASIC : RLC_MODE_EXTENDED;

/* 总线事务失败后，下次按键时可重新初始化 OLED。 */
static void EnsureDisplay(void)
{
    if (!display_ready)
    {
        display_ready = OLED_Init();
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
#elif RLC_APP_BASIC_ONLY
    ShowPage("RLC METER", "PRESS TO MEASURE", "BASIC AUTO", hint);
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

static void RunMeasurement(const RLC_Hardware *port, uint32_t command_ms)
{
    RLC_Result result;
    float feedback_ohm = 0.0f;

    if (display_ready)
    {
        display_ready = OLED_ShowActivity("MEASURING");
    }
#if RLC_APP_FIXED_1KHZ
    result = RLC_MeasureFixed(port, &active_calibration, RLC_FREQUENCY_1_KHZ, RLC_RANGE_1_KOHM);
#else
    result = RLC_MeasureAuto(port, &active_calibration, measurement_mode);
#endif
    if (result.range_index < RLC_RANGE_COUNT)
    {
        feedback_ohm = port->feedback_ohm[result.range_index];
    }
    if (display_ready)
    {
        display_ready = OLED_ShowResult(&result, feedback_ohm);
    }
    Serial_PrintResult(&result, BSP_GetMillis() - command_ms, display_ready);
}

/* 开路与短路两步都成功才写入 Flash。失败时留在当前步骤，短按可重试。 */
static void CaptureCalibrationStep(const RLC_Hardware *port)
{
    const bool open = app_page == APP_CALIBRATE_OPEN;
    const char *title = open ? "CAL OPEN" : "CAL SHORT";
    RLC_Status status;

    ShowPage(title, "CAPTURING", "", "PLEASE WAIT");
    status = RLC_CaptureCalibration(port, &pending_calibration, open, measurement_mode);
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

static void HandleButtonEvent(KeyEvent event, const RLC_Hardware *port, uint32_t command_ms)
{
    EnsureDisplay();
    /* 短按执行当前页面的动作。 */
    if (event == KEY_SHORT_PRESS)
    {
        if (app_page == APP_MEASURE)
        {
            RunMeasurement(port, command_ms);
        }
        else
        {
            CaptureCalibrationStep(port);
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
        HandleButtonEvent(event, BSP_GetMeasureHardware(), now_ms);
    }
}
