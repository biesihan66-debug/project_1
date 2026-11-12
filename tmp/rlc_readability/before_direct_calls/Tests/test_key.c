/*
 * 按键主机测试。BSP_KeyIsPressed() 提供模拟的 PB10 按下状态。
 * 每次检查把指定的毫秒数传入实际 Key_GetEvent()，核对应用收到的事件。
 */
#include "BSP_Board.h"
#include "Key.h"

#include <stdio.h>
#include <stdlib.h>

static bool key_pressed;

bool BSP_KeyIsPressed(void)
{
    return key_pressed;
}

static void CheckEvent(uint32_t now_ms, bool pressed, KeyEvent expected, const char *description)
{
    key_pressed = pressed;
    const KeyEvent actual = Key_GetEvent(now_ms);
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (event %d, expected %d)\n", description, (int)actual,
                (int)expected);
        exit(EXIT_FAILURE);
    }
}

static void TestShortPress(void)
{
    Key_Init();
    CheckEvent(100u, true, KEY_NONE, "press edge");
    CheckEvent(105u, false, KEY_NONE, "contact bounce");
    CheckEvent(125u, false, KEY_NONE, "bounce generates no event");

    CheckEvent(130u, true, KEY_NONE, "stable press begins");
    CheckEvent(149u, true, KEY_NONE, "19 ms press");
    CheckEvent(150u, true, KEY_NONE, "20 ms press");
    CheckEvent(151u, false, KEY_NONE, "release edge");
    CheckEvent(170u, false, KEY_NONE, "19 ms release");
    CheckEvent(171u, false, KEY_SHORT_PRESS, "20 ms release reports short press");
    CheckEvent(180u, false, KEY_NONE, "short press reported once");
}

static void TestLongPress(void)
{
    Key_Init();
    CheckEvent(1000u, true, KEY_NONE, "hold begins");
    CheckEvent(1020u, true, KEY_NONE, "hold debounced");
    CheckEvent(1899u, true, KEY_NONE, "899 ms hold");
    CheckEvent(1900u, true, KEY_LONG_PRESS, "900 ms hold reports long press");
    CheckEvent(2200u, true, KEY_NONE, "hold reports long press once");
    CheckEvent(2300u, false, KEY_NONE, "release after long hold");
    CheckEvent(2320u, false, KEY_NONE, "long hold release does not repeat event");
}

static void TestLatePolling(void)
{
    Key_Init();
    CheckEvent(100u, true, KEY_NONE, "late polling hold begins");
    CheckEvent(120u, true, KEY_NONE, "late polling hold debounced");
    CheckEvent(1000u, false, KEY_NONE, "polling resumes at release");
    CheckEvent(1019u, false, KEY_NONE, "late polling release still bouncing");
    CheckEvent(1020u, false, KEY_LONG_PRESS, "release recognizes 900 ms hold");
    CheckEvent(1030u, false, KEY_NONE, "late polling reports long press once");
}

static void TestTimerWrapAndReset(void)
{
    Key_Init();
    CheckEvent(UINT32_MAX - 9u, true, KEY_NONE, "press before timer wraps");
    CheckEvent(9u, true, KEY_NONE, "19 ms press across wrap");
    CheckEvent(10u, true, KEY_NONE, "20 ms press across wrap");
    CheckEvent(20u, false, KEY_NONE, "release after wrap");
    CheckEvent(40u, false, KEY_SHORT_PRESS, "short press recognized across wrap");

    CheckEvent(100u, true, KEY_NONE, "hold before reset");
    CheckEvent(120u, true, KEY_NONE, "hold before reset debounced");
    CheckEvent(1000u, true, KEY_LONG_PRESS, "long press reported before reset");
    Key_Init();
    CheckEvent(1010u, true, KEY_NONE, "reset while key is held starts a fresh press");
    CheckEvent(1030u, true, KEY_NONE, "fresh press debounced");
    CheckEvent(1040u, false, KEY_NONE, "fresh press released");
    CheckEvent(1060u, false, KEY_SHORT_PRESS, "reset clears previous long press");
}

int main(void)
{
    TestShortPress();
    TestLongPress();
    TestLatePolling();
    TestTimerWrapAndReset();
    puts("PASS: key debounce, short and long press, late polling, timer wrap, and reset");
    return EXIT_SUCCESS;
}
