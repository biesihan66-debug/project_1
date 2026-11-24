/*
 * 按键接口：读取 PB10，识别短按与长按。硬件引脚在 BSP_PinMap.h。
 * Key_Init() 只清除去抖状态；GPIO 上拉由 BSP_Init() 配置。
 * Key_GetEvent(now_ms) 接收当前毫秒数，无等待延时；每轮主循环调用一次。
 * 返回 KEY_NONE 表示没有新动作；短按在松开后产生，长按在按住 900 ms 时产生一次。
 * 本模块不显示文字。App_RLC.c 收到事件后切换 OLED 页面或启动测量。
 */
#ifndef KEY_H
#define KEY_H

#include <stdint.h>

typedef enum
{
    KEY_NONE,
    KEY_SHORT_PRESS,
    KEY_LONG_PRESS
} KeyEvent;

void Key_Init(void);
uint32_t Key_PressedAt(void);
KeyEvent Key_GetEvent(uint32_t now_ms);

#endif
