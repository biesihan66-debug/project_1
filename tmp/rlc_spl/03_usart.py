from support import add_spl, read, replace, replace_region, write


add_spl('usart')
path = 'System/BSP_USART.c'
uart = read(path)
uart = replace(uart, '#include "BSP_Registers.h"\n', '')
uart = replace_region(uart, '#define USART_SR', 'void BSP_USART_Init',
    '/* BSP_USART_Init() 启用 USART1 后置位，初始化失败时避免访问串口。 */\n'
    'static bool uart_ready;\n\n')
uart = replace(uart, '    GPIO_InitTypeDef gpio_init;',
               '    GPIO_InitTypeDef gpio_init;\n    USART_InitTypeDef usart_init;')
uart = replace(uart, '    STM32_RCC_APB2ENR |= 1u << 14;',
               '    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);\n'
               '    USART_DeInit(USART1);')
uart = replace(uart,
    '    USART_BRR = (RLC_SYSTEM_CLOCK_HZ + RLC_DEBUG_BAUD / 2u) / RLC_DEBUG_BAUD;\n'
    '    USART_CR1 = (1u << 13) | (1u << 3); /* 8-N-1，只启用发送。 */',
    '''    usart_init.USART_BaudRate = RLC_DEBUG_BAUD;
    usart_init.USART_WordLength = USART_WordLength_8b;
    usart_init.USART_StopBits = USART_StopBits_1;
    usart_init.USART_Parity = USART_Parity_No;
    usart_init.USART_Mode = USART_Mode_Tx;
    usart_init.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_Init(USART1, &usart_init);
    USART_Cmd(USART1, ENABLE);
    uart_ready = true;''')
uart = replace(uart, '(USART_CR1 & (1u << 13)) == 0u', '!uart_ready')
uart = replace(uart, '(USART_SR & (1u << 7)) == 0u', 'USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET')
uart = replace(uart, '        USART_DR = (uint8_t)*text++;', '        USART_SendData(USART1, (uint8_t)*text++);')
write(path, uart)
