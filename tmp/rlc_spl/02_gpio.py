import re

from support import add_project_files, add_spl, edit, read, replace, replace_region, write


add_spl('gpio')
add_project_files('System - Hardware', ['System/BSP_GPIO.c', 'System/BSP_GPIO.h',
                                      'System/BSP_USART.c', 'System/BSP_USART.h'])

# This header stores wiring only. The peripheral modules include stm32f10x.h.
path = 'System/BSP_PinMap.h'
pins = read(path)
pins = replace(pins, '#include "BSP_Registers.h"\n\n', '')
pins = replace(pins, ' * 为同一个 GPIO 端口上的三位档位控制。实际反馈阻容位于 BSP_Board.c。\n',
               ' * 为同一个 GPIO 端口上的三位档位控制。各 PIN 为 SPL 位掩码。\n'
               ' * 实际反馈阻容位于 BSP_Board.c。\n')
pins = pins.replace('STM32_GPIOA', 'GPIOA').replace('STM32_GPIOB', 'GPIOB')
pins, count = re.subn(r'(?m)(^#define RLC_\w+_PIN +)(\d+)u$', r'\g<1>GPIO_Pin_\2', pins)
assert count == 14, count
write(path, pins)

write('System/BSP_GPIO.h', '''/*
 * 由 BSP_Init() 启用 GPIOA/GPIOB 和 AFIO，先设量程安全档，再配置按键。
 * 按键电平经 BSP_KeyIsPressed() 供 Key.c 判断，不直接写 OLED。
 */
#ifndef BSP_GPIO_H
#define BSP_GPIO_H

void BSP_GPIO_Init(void);

#endif
''')
write('System/BSP_GPIO.c', '''/*
 * 板级 GPIO 入口。PB10 内部上拉，按下为低电平。
 * gpio_init 只在 BSP_GPIO_Init() 内使用；量程输出由 Range_Switch.c 初始化。
 * BSP_KeyIsPressed() 由 Key.c 读取，测量和校准页面由 App_RLC.c 更新。
 */
#include "BSP_GPIO.h"
#include "stm32f10x.h"
#include "BSP_PinMap.h"
#include "BSP_Board.h"
#include "Range_Switch.h"

void BSP_GPIO_Init(void)
{
    GPIO_InitTypeDef gpio_init;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_GPIOB | RCC_APB2Periph_AFIO,
                           ENABLE);
    Range_Init();

    GPIO_SetBits(RLC_BUTTON_GPIO, RLC_BUTTON_PIN);
    gpio_init.GPIO_Pin = RLC_BUTTON_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_IPU;
    gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(RLC_BUTTON_GPIO, &gpio_init);
}

bool BSP_KeyIsPressed(void)
{
    return GPIO_ReadInputDataBit(RLC_BUTTON_GPIO, RLC_BUTTON_PIN) == Bit_RESET;
}
''')
write('System/BSP_USART.h', '''/*
 * USART1 调试口初始化，由 BSP_Init() 在 GPIO 就绪后调用。
 * BSP_DebugWrite() 的声明位于 BSP_Board.h，串口数据不进入 OLED。
 */
#ifndef BSP_USART_H
#define BSP_USART_H

void BSP_USART_Init(void);

#endif
''')

path = 'System/BSP_Board.c'
board = read(path)
uart_registers = board[board.index('#define USART_SR'):board.index('/* 上电时激励停用')]
uart_write = board[board.index('void BSP_DebugWrite'):board.index('void BSP_StopMeasurement')]
write('System/BSP_USART.c', '''/*
 * USART1 调试输出。PA9 发送，PA10 保持浮空输入，115200 波特、8-N-1。
 * text 由调用方提供，deadline_ms 是本次写入的绝对截止时刻。
 * App_RLC.c 在 OLED 刷新后调用串口诊断，本模块不写 OLED。
 */
#include "BSP_USART.h"
#include "stm32f10x.h"
#include "BSP_PinMap.h"
#include "BSP_Board.h"
#include "BSP_Registers.h"

''' + uart_registers + '''void BSP_USART_Init(void)
{
    GPIO_InitTypeDef gpio_init;

    STM32_RCC_APB2ENR |= 1u << 14;
    gpio_init.GPIO_Pin = RLC_DEBUG_TX_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(RLC_DEBUG_GPIO, &gpio_init);

    gpio_init.GPIO_Pin = RLC_DEBUG_RX_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(RLC_DEBUG_GPIO, &gpio_init);

    USART_BRR = (RLC_SYSTEM_CLOCK_HZ + RLC_DEBUG_BAUD / 2u) / RLC_DEBUG_BAUD;
    USART_CR1 = (1u << 13) | (1u << 3); /* 8-N-1，只启用发送。 */
}

''' + uart_write.rstrip() + '\n')
board = replace(board, '#include "BSP_Clock.h"\n',
                '#include "BSP_Clock.h"\n#include "BSP_GPIO.h"\n#include "BSP_USART.h"\n')
board = replace(board, uart_registers, '')
board = replace_region(board, 'static void InitializeButtonAndUart', 'bool BSP_Init', '')
board = replace(board,
                '    STM32_RCC_APB2ENR |= (1u << 0) | (1u << 2) | (1u << 3);\n'
                '    Range_Init();\n    InitializeButtonAndUart();\n',
                '    BSP_GPIO_Init();\n    BSP_USART_Init();\n')
board = replace_region(board, 'bool BSP_KeyIsPressed', 'void BSP_StopMeasurement', '')
board = board[:board.index('/* GPIO 基础操作')].rstrip() + '\n'
write(path, board)

path = 'System/BSP_Registers.h'
legacy = read(path)
legacy = replace_region(legacy, '/*', '#ifndef',
                        '/* SPL 迁移期间，尚未迁移的外设继续使用这些旧地址。 */\n')
legacy = replace_region(legacy, '#define STM32_GPIO_ANALOG_IN', '#endif', '')
legacy = replace(legacy, '#define STM32_GPIOA          0x40010800u\n', '')
legacy = replace(legacy, '#define STM32_GPIOB          0x40010C00u\n', '')
write(path, legacy)

path = 'System/Range_Switch.c'
switch = read(path)
switch = replace(switch, '#include "BSP_PinMap.h"\n#include "Range_Switch.h"\n#include "BSP_Board.h"',
                 '#include "Range_Switch.h"\n#include "stm32f10x.h"\n#include "BSP_PinMap.h"\n#include "RLC_Types.h"')
switch = switch[:switch.index('void Range_Set')] + '''#define RANGE_SELECT_PINS (RLC_RANGE_BIT0_PIN | RLC_RANGE_BIT1_PIN | RLC_RANGE_BIT2_PIN)

void Range_Set(uint8_t range_index)
{
    const uint8_t code = range_output_code[range_index];
    uint32_t set_pins = 0u;

    if ((code & 1u) != 0u)
    {
        set_pins |= RLC_RANGE_BIT0_PIN;
    }
    if ((code & 2u) != 0u)
    {
        set_pins |= RLC_RANGE_BIT1_PIN;
    }
    if ((code & 4u) != 0u)
    {
        set_pins |= RLC_RANGE_BIT2_PIN;
    }
    /* SPL 没有一次置位与复位不同引脚的接口。一次写 BSRR，避免短暂选到中间档。 */
    RLC_RANGE_GPIO->BSRR = ((RANGE_SELECT_PINS & ~set_pins) << 16) | set_pins;
}

void Range_Init(void)
{
    GPIO_InitTypeDef gpio_init;

    Range_Set(RLC_RANGE_SAFE_INDEX);
    gpio_init.GPIO_Pin = RANGE_SELECT_PINS;
    gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(RLC_RANGE_GPIO, &gpio_init);
}
'''
write(path, switch)

for name in ('ADC_Sample', 'DDS_Signal', 'OLED_I2C', 'Flash_Calibration'):
    path = f'System/{name}.c'
    includes = '#include "stm32f10x.h"\n' if name != 'Flash_Calibration' else ''
    edit(path, '#include "BSP_PinMap.h"\n', includes + '#include "BSP_PinMap.h"\n'
         '#include "BSP_Registers.h"\n')

path = 'System/ADC_Sample.c'
edit(path, '''    BSP_GPIO_Config(RLC_ADC_GPIO, RLC_ADC_DUT_PIN, STM32_GPIO_ANALOG_IN);
    BSP_GPIO_Config(RLC_ADC_GPIO, RLC_ADC_TIA_PIN, STM32_GPIO_ANALOG_IN);''', '''    gpio_init.GPIO_Pin = RLC_ADC_DUT_PIN | RLC_ADC_TIA_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_AIN;
    gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(RLC_ADC_GPIO, &gpio_init);''')
edit(path, 'bool ADC_Init(void)\n{\n    uint32_t ready_ms;',
     'bool ADC_Init(void)\n{\n    GPIO_InitTypeDef gpio_init;\n    uint32_t ready_ms;')

path = 'System/DDS_Signal.c'
dds = read(path)
dds = replace(dds, 'BSP_GPIO_Write(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN, false);',
              'GPIO_ResetBits(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN);')
dds = replace(dds, 'BSP_GPIO_Write(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN, true);',
              'GPIO_SetBits(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN);', 2)
dds = replace_region(dds, 'bool DDS_Init', '    STM32_RCC_CFGR =', '''bool DDS_Init(void)
{
    GPIO_InitTypeDef gpio_init;

    STM32_RCC_APB2ENR |= 1u << 12;
    GPIO_SetBits(RLC_DDS_GPIO, RLC_DDS_SELECT_PIN);
    gpio_init.GPIO_Pin = RLC_DDS_SELECT_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_Out_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(RLC_DDS_GPIO, &gpio_init);

    gpio_init.GPIO_Pin = RLC_DDS_CLOCK_PIN | RLC_DDS_DATA_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(RLC_DDS_GPIO, &gpio_init);
#if RLC_AD9833_USE_MCO
    gpio_init.GPIO_Pin = RLC_DDS_MCO_PIN;
    GPIO_Init(RLC_DDS_GPIO, &gpio_init);
''')
write(path, dds)

path = 'System/OLED_I2C.c'
oled = read(path)
oled = replace(oled, 'uint8_t pin, bool high', 'uint16_t pin, bool high', 2)
oled = replace(oled, 'BSP_GPIO_Read(RLC_OLED_GPIO, pin) != high',
               '(GPIO_ReadInputDataBit(RLC_OLED_GPIO, pin) == Bit_SET) != high')
oled = replace(oled, 'BSP_GPIO_Write(RLC_OLED_GPIO, pin, high);',
               'GPIO_WriteBit(RLC_OLED_GPIO, pin, high ? Bit_SET : Bit_RESET);')
oled = replace(oled, '    const uint32_t output_open_drain = 0x6u; /* 通用开漏输出，2 MHz。 */',
               '    GPIO_InitTypeDef gpio_init;')
oled = replace(oled, '    BSP_GPIO_Write(RLC_OLED_GPIO, RLC_OLED_SCL_PIN, true);\n'
                     '    BSP_GPIO_Write(RLC_OLED_GPIO, RLC_OLED_SDA_PIN, true);',
               '    GPIO_SetBits(RLC_OLED_GPIO, RLC_OLED_SCL_PIN | RLC_OLED_SDA_PIN);', 2)
oled = replace(oled, '    BSP_GPIO_Config(RLC_OLED_GPIO, RLC_OLED_SCL_PIN, output_open_drain);\n'
                     '    BSP_GPIO_Config(RLC_OLED_GPIO, RLC_OLED_SDA_PIN, output_open_drain);',
               '    gpio_init.GPIO_Pin = RLC_OLED_SCL_PIN | RLC_OLED_SDA_PIN;\n'
               '    gpio_init.GPIO_Mode = GPIO_Mode_Out_OD;\n'
               '    gpio_init.GPIO_Speed = GPIO_Speed_2MHz;\n'
               '    GPIO_Init(RLC_OLED_GPIO, &gpio_init);')
oled = replace(oled, '    BSP_GPIO_Config(RLC_OLED_GPIO, RLC_OLED_SCL_PIN, STM32_GPIO_AF_OD);\n'
                     '    BSP_GPIO_Config(RLC_OLED_GPIO, RLC_OLED_SDA_PIN, STM32_GPIO_AF_OD);',
               '    gpio_init.GPIO_Mode = GPIO_Mode_AF_OD;\n'
               '    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;\n'
               '    GPIO_Init(RLC_OLED_GPIO, &gpio_init);')
write(path, oled)

path = 'Tests/test_board_deadline.c'
edit(path, '#include "BSP_Clock.h"\n',
     '#include "BSP_Clock.h"\n#include "BSP_GPIO.h"\n#include "BSP_USART.h"\n')
edit(path, 'void Range_Init(void) {}\n', 'void BSP_GPIO_Init(void) {}\nvoid BSP_USART_Init(void) {}\n')
