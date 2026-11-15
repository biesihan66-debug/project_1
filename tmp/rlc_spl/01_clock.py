import re

from support import PROJECT, add_project_files, add_spl, copy_reference, edit, read, replace, replace_region, write


# Use the reference CMSIS and medium-density vectors, retaining this project's memory budgets.
for name in ('stm32f10x.h', 'core_cm3.h', 'system_stm32f10x.h', 'system_stm32f10x.c',
             'startup_stm32f10x_md.s'):
    copy_reference('start/' + name, 'startup/' + name)

startup_path = 'startup/startup_stm32f10x_md.s'
startup = read(startup_path)
startup = '; RLC V1.1: Reset runs SystemInit, then the C runtime. BSP_Clock_Init starts the PLL.\n' + startup
startup, stack_count = re.subn(r'(Stack_Size\s+EQU\s+)0x00000400', r'\g<1>0x00000800', startup)
startup, heap_count = re.subn(r'(Heap_Size\s+EQU\s+)0x00000200', r'\g<1>0x00000100', startup)
assert stack_count == heap_count == 1
startup = replace(startup, '     IMPORT  SystemInit\n',
                  '     IMPORT  SystemInit\n     IMPORT  __use_no_semihosting\n')
startup, count = re.subn(r'(?m)^\s+END\s*$',
    '\n_sys_exit        PROC\n                 EXPORT  _sys_exit\n                 B       _sys_exit\n                 ENDP\n\n                 END\n', startup)
assert count == 1
write(startup_path, startup)

# SystemInit is called before the C runtime. Stay on HSI there so all PLL startup
# waits and their errors are handled once by the application-time BSP clock setup.
edit('startup/system_stm32f10x.c', '#define SYSCLK_FREQ_72MHz  72000000',
     '/* RLC V1.1: use HSI at reset. BSP_Clock_Init sets up 72 MHz with timeouts. */\n'
     '/* #define SYSCLK_FREQ_72MHz  72000000 */')
copy_reference('USER/stm32f10x_conf.h')
path = 'USER/stm32f10x_conf.h'
conf = replace_region(read(path), '/* Uncomment/Comment the line below', '/* Exported types',
                      '/* SPL drivers used by this project. */\n\n')
write(path, conf)

project = read('project.uvprojx')
project = replace(project, 'startup_stm32f103c8.s', 'startup_stm32f10x_md.s', 6)
project = replace(project, '<IncludePath>.\\System</IncludePath>',
    '<IncludePath>.\\startup;.\\USER;.\\Library;.\\System</IncludePath>', 3)
pattern = r'(<Cads>.*?<Define>)([^<]*)(</Define>)'
project, count = re.subn(pattern, lambda m: m[1] + 'STM32F10X_MD,USE_STDPERIPH_DRIVER'
                        + (',' + m[2] if m[2] else '') + m[3], project, flags=re.S)
assert count == 3
write('project.uvprojx', project)
add_project_files('Startup', [f'startup/{name}' for name in ('stm32f10x.h', 'core_cm3.h',
                  'system_stm32f10x.c', 'system_stm32f10x.h')])
add_project_files('USER - Entry', ['USER/stm32f10x_conf.h'])
add_project_files('System - Hardware', ['System/BSP_Clock.c', 'System/BSP_Clock.h'])
add_spl('rcc', 'flash')

write('System/BSP_Clock.h', '''/*
 * F103 板级系统时钟与 1 ms SysTick。由 BSP_Init() 在外设初始化前调用。
 * 返回 true 表示 72 MHz 时钟和计时可用；失败时应用停止初始化。
 * SysTick_Handler() 和 uptime_ms 在 BSP_Board.c。本模块不输出到 OLED。
 */
#ifndef BSP_CLOCK_H
#define BSP_CLOCK_H

#include <stdbool.h>

bool BSP_Clock_Init(void);

#endif
''')
write('System/BSP_Clock.c', '''/*
 * 系统时钟从 HSI 切至 8 MHz HSE ×9 PLL，HCLK 为 72 MHz。
 * APB1 /2、APB2 /1、ADC /6，TIM3 仍为 72 MHz。
 * CLOCK_WAIT_ATTEMPTS 是 SysTick 启用前的最大轮询次数。
 * BSP_Clock_Init() 启用 1 ms SysTick，BSP_Board.c 的 uptime_ms 在中断中递增。
 * 本模块不直接更新 OLED。
 */
#include "BSP_Clock.h"
#include "stm32f10x.h"

#define CLOCK_WAIT_ATTEMPTS 500000u

static bool WaitClockFlag(uint8_t flag, FlagStatus expected)
{
    uint32_t remaining = CLOCK_WAIT_ATTEMPTS;

    while (RCC_GetFlagStatus(flag) != expected && remaining != 0u)
    {
        --remaining;
    }
    return remaining != 0u;
}

static bool WaitClockSource(uint8_t source)
{
    uint32_t remaining = CLOCK_WAIT_ATTEMPTS;

    while (RCC_GetSYSCLKSource() != source && remaining != 0u)
    {
        --remaining;
    }
    return remaining != 0u;
}

bool BSP_Clock_Init(void)
{
    RCC_HSICmd(ENABLE);
    if (!WaitClockFlag(RCC_FLAG_HSIRDY, SET))
    {
        return false;
    }
    RCC_SYSCLKConfig(RCC_SYSCLKSource_HSI);
    if (!WaitClockSource(RCC_CFGR_SWS_HSI))
    {
        return false;
    }
    RCC_PLLCmd(DISABLE);
    if (!WaitClockFlag(RCC_FLAG_PLLRDY, RESET))
    {
        return false;
    }
    RCC_HSEConfig(RCC_HSE_ON);
    if (!WaitClockFlag(RCC_FLAG_HSERDY, SET))
    {
        return false;
    }

    FLASH_PrefetchBufferCmd(FLASH_PrefetchBuffer_Enable);
    FLASH_SetLatency(FLASH_Latency_2);
    RCC_HCLKConfig(RCC_SYSCLK_Div1);
    RCC_PCLK1Config(RCC_HCLK_Div2);
    RCC_PCLK2Config(RCC_HCLK_Div1);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6);
    RCC_PLLConfig(RCC_PLLSource_HSE_Div1, RCC_PLLMul_9);
    RCC_PLLCmd(ENABLE);
    if (!WaitClockFlag(RCC_FLAG_PLLRDY, SET))
    {
        return false;
    }
    RCC_SYSCLKConfig(RCC_SYSCLKSource_PLLCLK);
    if (!WaitClockSource(RCC_CFGR_SWS_PLL))
    {
        return false;
    }

    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000u) != 0u)
    {
        return false;
    }
    NVIC_SetPriority(SysTick_IRQn, 0u);
    return true;
}
''')

path = 'System/BSP_Board.c'
board = read(path)
board = replace(board, '#include "BSP_Board.h"\n', '#include "BSP_Board.h"\n#include "BSP_Clock.h"\n')
board = replace_region(board, '#define RCC_CR_HSION', '#define USART_SR', '')
board = replace_region(board, 'static bool WaitForClock', 'static void InitializeButtonAndUart', '')
board = replace(board, '    if (!InitializeClocks())', '    uptime_ms = 0u;\n    if (!BSP_Clock_Init())')
board = replace(board, '    InitializeTick();\n', '')
write(path, board)
edit('Tests/test_board_deadline.c', '#include "BSP_Board.h"\n',
     '#include "BSP_Board.h"\n#include "BSP_Clock.h"\n')
edit('Tests/test_board_deadline.c', 'void Range_Init(void) {}\n',
     'bool BSP_Clock_Init(void)\n{\n    return true;\n}\n\nvoid Range_Init(void) {}\n')
