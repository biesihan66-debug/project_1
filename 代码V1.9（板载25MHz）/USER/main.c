/*
 * RLC 电桥 V1.9 程序入口：先初始化，再反复处理按键和测量。
 * 功能实现均封装在 System；具体流程从 App_RLC.c 开始阅读。
 * 本文件没有测量变量。结果保存在 RLC_Result，显示位置见 OLED_Display.h。
 */
#include "App_RLC.h"
#include "BSP_Board.h"

int main(void)
{
    if (!App_Init())
    {
        while (1)
        {
            /* 初始化失败后只处理未送达的停波指令，待复位后重新初始化。 */
            BSP_RetryPendingStop();
        }
    }

    while (1)
    {
        App_Run();
    }
}
