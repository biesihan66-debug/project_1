/*
 * RLC 电桥 V1.4 程序入口：先初始化，再反复处理按键和测量。
 * 功能实现均封装在 System；具体流程从 App_RLC.c 开始阅读。
 * 本文件没有测量变量。结果保存在 RLC_Result，显示位置见 OLED_Display.h。
 */
#include "App_RLC.h"

int main(void)
{
    if (!App_Init())
    {
        while (1)
        {
            /* 板级初始化失败，等待复位后重新初始化。 */
        }
    }

    while (1)
    {
        App_Run();
    }
}
