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
