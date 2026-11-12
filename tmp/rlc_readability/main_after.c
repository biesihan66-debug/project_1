/*
 * 电桥主程序。PB10 为低有效按键，电平通过 stm32_rlc_board.h 读取。
 * 主循环只做两件事：读取已去抖的按键事件，然后处理测量或校准。
 * 短按：测量页启动测量；校准页采集当前步骤。长按：进入或退出校准。
 *
 * 变量均在本文件内：
 * button 记录按键电平、去抖时间和长按是否已上报，时间来自 SysTick，单位 ms。
 * app_page 记录测量/开路校准/短路校准页面；display_ready 记录 OLED 是否可用。
 * measurement_mode 按 Keil 构建选项确定基础/扩展模式，不在运行中变化。
 * active_calibration 供测量读，pending_calibration 由开短路流程写，
 * 两者是本文件的静态变量。校准数组下标见 open_short_calibration.h，
 * 配对成功并写入板级 Flash 后才更新 active_calibration。
 * run_measurement() 的 result 在本次调用栈上，类型/数值/频率/量程分别
 * 送到 OLED 第 1/2/3/4 行，I/Q、复阻抗和用时经 USART 输出。
 * RLC_APP_FIXED_1KHZ 用于 1 kHz / 1 kΩ 固定台架，RLC_APP_BASIC_ONLY
 * 用于基础三频验证，常规构建启用含 100 kHz 的自动测量。
 */
#include "automatic_rlc_measurement.h"
#include "measurement_debug_output.h"
#include "oled_result_display.h"
#include "stm32_rlc_board.h"

#ifndef RLC_APP_FIXED_1KHZ
#define RLC_APP_FIXED_1KHZ 0
#endif

#ifndef RLC_APP_BASIC_ONLY
#define RLC_APP_BASIC_ONLY 0
#endif

enum {
    BUTTON_DEBOUNCE_MS = 20u,
    BUTTON_LONG_PRESS_MS = 900u
};

typedef enum {
    BUTTON_NO_EVENT,
    BUTTON_SHORT_PRESS,
    BUTTON_LONG_PRESS
} ButtonEvent;

typedef struct {
    bool raw_pressed;          /* 本次直接读到的按键状态。 */
    bool stable_pressed;       /* 持续稳定至少 20 ms 后接受的状态。 */
    bool long_reported;        /* 本轮按住期间是否已经发过长按事件。 */
    uint32_t raw_changed_ms;   /* 原始电平最近一次改变的时刻。 */
    uint32_t pressed_ms;       /* 本轮有效按下的起始时刻。 */
} ButtonState;

typedef enum {
    APP_MEASURE,
    APP_CALIBRATE_OPEN,
    APP_CALIBRATE_SHORT
} AppPage;

/* 只有主循环访问这些状态，中断负责板级计时和 DMA。 */
static ButtonState button;
static AppPage app_page = APP_MEASURE;
static RlcCalibration active_calibration;
static RlcCalibration pending_calibration;
static bool display_ready;
static const RlcMeasurementMode measurement_mode =
    RLC_APP_BASIC_ONLY ? RLC_MODE_BASIC : RLC_MODE_EXTENDED;

/* 短按在去抖后的释放时触发，长按达到阈值时触发且只报告一次。 */
static ButtonEvent read_button_event(uint32_t now_ms)
{
    const bool raw_pressed = rlc_board_button_pressed();
    /* 1. 电平变化时重新开始去抖计时。 */
    if (button.raw_pressed != raw_pressed) {
        button.raw_pressed = raw_pressed;
        button.raw_changed_ms = now_ms;
    }

    /* 2. 接受稳定电平；释放时只上报尚未触发过的短按或长按。 */
    if (raw_pressed != button.stable_pressed &&
        now_ms - button.raw_changed_ms >= BUTTON_DEBOUNCE_MS) {
        button.stable_pressed = raw_pressed;
        if (raw_pressed) {
            button.pressed_ms = button.raw_changed_ms;
            button.long_reported = false;
        } else {
            if (button.long_reported) {
                return BUTTON_NO_EVENT;
            }
            if (button.raw_changed_ms - button.pressed_ms >= BUTTON_LONG_PRESS_MS) {
                return BUTTON_LONG_PRESS;
            }
            return BUTTON_SHORT_PRESS;
        }
    }
    /* 3. 持续按住超过 900 ms，立即上报一次长按，无需等到松手。 */
    if (raw_pressed && button.stable_pressed && !button.long_reported &&
        now_ms - button.pressed_ms >= BUTTON_LONG_PRESS_MS) {
        button.long_reported = true;
        return BUTTON_LONG_PRESS;
    }
    return BUTTON_NO_EVENT;
}

/* 总线事务失败后，下次按键时可重新初始化 OLED。 */
static void ensure_display(void)
{
    if (!display_ready) {
        display_ready = oled_result_display_initialize();
    }
}

static void show_page(const char *line1, const char *line2,
                      const char *line3, const char *line4)
{
    if (display_ready) {
        display_ready = oled_result_display_show_lines(line1, line2, line3, line4);
    }
}

static void show_ready(const char *hint)
{
#if RLC_APP_FIXED_1KHZ
    show_page("RLC METER", "PRESS TO MEASURE", "FIXED 1 KHZ", hint);
#elif RLC_APP_BASIC_ONLY
    show_page("RLC METER", "PRESS TO MEASURE", "BASIC AUTO", hint);
#else
    show_page("RLC METER", "PRESS TO MEASURE", "EXTENDED AUTO", hint);
#endif
}

static void show_calibration_prompt(const char *hint)
{
    if (app_page == APP_CALIBRATE_OPEN) {
        show_page("CAL OPEN", "REMOVE COMPONENT", "PRESS WHEN READY", hint);
    } else {
        show_page("CAL SHORT", "SHORT THE CLIP", "PRESS WHEN READY", hint);
    }
}

static void run_measurement(const RlcMeasurementPort *port, uint32_t command_ms)
{
    RlcMeasurementResult result;
    float feedback_ohm = 0.0f;

    if (display_ready) {
        display_ready = oled_result_display_show_activity("MEASURING");
    }
#if RLC_APP_FIXED_1KHZ
    result = rlc_measure_fixed(port, &active_calibration,
                               RLC_FREQUENCY_1_KHZ, RLC_RANGE_1_KOHM);
#else
    result = rlc_measure_auto(port, &active_calibration, measurement_mode);
#endif
    if (result.range_index < RLC_RANGE_COUNT) {
        feedback_ohm = port->feedback_ohm[result.range_index];
    }
    if (display_ready) {
        display_ready = oled_result_display_show_measurement(&result, feedback_ohm);
    }
    measurement_debug_output_result(&result, rlc_board_millis() - command_ms,
                                    display_ready);
}

/* 开路与短路两步都成功才写入 Flash。失败时留在当前步骤，短按可重试。 */
static void capture_calibration_step(const RlcMeasurementPort *port)
{
    const bool open = app_page == APP_CALIBRATE_OPEN;
    const char *title = open ? "CAL OPEN" : "CAL SHORT";
    RlcStatus status;

    show_page(title, "CAPTURING", "", "PLEASE WAIT");
    status = rlc_capture_calibration(port, &pending_calibration, open, measurement_mode);
    if (status != RLC_STATUS_OK) {
        show_calibration_prompt(oled_result_display_status_text(status));
        measurement_debug_output_calibration(open, status);
        return;
    }
    if (open) {
        app_page = APP_CALIBRATE_SHORT;
        show_calibration_prompt("HOLD TO CANCEL");
        measurement_debug_output_calibration(true, RLC_STATUS_OK);
        return;
    }
    if (!rlc_board_save_calibration(&pending_calibration, sizeof pending_calibration)) {
        show_calibration_prompt("FLASH SAVE ERROR");
        measurement_debug_output_calibration(false, RLC_STATUS_CALIBRATION_ERROR);
        return;
    }
    active_calibration = pending_calibration;
    app_page = APP_MEASURE;
    show_ready("CAL SAVED");
    measurement_debug_output_calibration(false, RLC_STATUS_OK);
}

static void handle_button_event(ButtonEvent event, const RlcMeasurementPort *port,
                                uint32_t command_ms)
{
    ensure_display();
    /* 短按执行当前页面的动作。 */
    if (event == BUTTON_SHORT_PRESS) {
        if (app_page == APP_MEASURE) {
            run_measurement(port, command_ms);
        } else {
            capture_calibration_step(port);
        }
        return;
    }

    /* 长按只负责切换页面；固定台架模式不进入自动校准。 */
    if (RLC_APP_FIXED_1KHZ) {
        show_ready("BENCH TEST");
    } else if (app_page == APP_MEASURE) {
        pending_calibration = (RlcCalibration){0};
        app_page = APP_CALIBRATE_OPEN;
        show_calibration_prompt("HOLD TO CANCEL");
    } else {
        app_page = APP_MEASURE;
        show_ready("CAL CANCELLED");
    }
}

int main(void)
{
    const RlcMeasurementPort *port;

    if (!rlc_board_initialize()) {
        rlc_board_debug_write("BOARD INIT ERROR\r\n");
        for (;;) {
        }
    }
    port = rlc_board_measurement_port();
    if (rlc_board_load_calibration(&pending_calibration, sizeof pending_calibration)) {
        active_calibration = pending_calibration;
    }
    ensure_display();
    show_ready(RLC_APP_FIXED_1KHZ ? "BENCH TEST" : "HOLD FOR CAL");
    rlc_board_debug_write("RLC READY\r\n");

    for (;;) {
        const uint32_t now_ms = rlc_board_millis();
        const ButtonEvent event = read_button_event(now_ms);
        if (event != BUTTON_NO_EVENT) {
            handle_button_event(event, port, now_ms);
        }
    }
}
