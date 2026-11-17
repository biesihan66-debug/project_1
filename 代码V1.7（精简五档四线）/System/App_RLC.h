/*
 * 电桥应用入口。USER/main.c 只需要包含这一个头文件。
 * 阅读顺序：main.c -> App_RLC.c（按键和校准流程）-> RLC_Measure.h（测量接口）。
 * 测量结果在 RLC_Types.h 的 RLC_Result 中；OLED 第 1~4 行为类型、数值、频率、反馈档/错误。
 * 本头文件不定义共享变量，应用状态保存在 App_RLC.c 内部。
 */
#ifndef APP_RLC_H
#define APP_RLC_H

#include <stdbool.h>

/* 初始化硬件、按键和校准记录，显示待机页。成功为 true；失败时不要继续测量。 */
bool App_Init(void);

/* 在主循环内反复调用：读取按键，执行一次测量或当前校准步骤，无需传参数。 */
void App_Run(void);

#endif
