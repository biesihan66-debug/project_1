from support import add_spl, read, replace, replace_region, write


add_spl('i2c')
path = 'System/OLED_I2C.c'
oled = read(path)
oled = replace(oled, '#include "BSP_Registers.h"\n', '')
oled = replace(oled, ' * 屏幕行布局由 OLED_Display.c 管理，传输层只发送命令或像素。',
               ' * i2c_ready 记录 I2C1 是否初始化；deadline_ms 是本次总线操作的截止时刻。\n'
               ' * 屏幕行布局由 OLED_Display.c 管理，传输层只发送命令或像素。')
oled = replace_region(oled, '#define I2C_CR1', 'static bool WaitBusPin',
    '''#define OLED_I2C_ERROR_FLAGS (I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_AF | \\
                             I2C_FLAG_OVR | I2C_FLAG_TIMEOUT)

static bool i2c_ready;

''')
oled = replace(oled, '    I2C_CR1 = 0u;\n    GPIO_SetBits',
               '    I2C_Cmd(I2C1, DISABLE);\n    GPIO_SetBits')
oled = replace(oled, '    I2C_CR1 = I2C_SOFT_RESET;\n    I2C_CR1 = 0u;',
               '    I2C_SoftwareResetCmd(I2C1, ENABLE);\n    I2C_SoftwareResetCmd(I2C1, DISABLE);')
oled = replace_region(oled, 'void OLED_I2C_Init', 'static bool StartDisplayTransfer',
    '''void OLED_I2C_Init(void)
{
    I2C_InitTypeDef i2c_init;

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);
    I2C_DeInit(I2C1);
    ReleaseI2CAnalogFilters();

    i2c_init.I2C_ClockSpeed = RLC_OLED_I2C_CLOCK_HZ;
    i2c_init.I2C_Mode = I2C_Mode_I2C;
    i2c_init.I2C_DutyCycle = I2C_DutyCycle_2;
    i2c_init.I2C_OwnAddress1 = 0u;
    i2c_init.I2C_Ack = I2C_Ack_Disable;
    i2c_init.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    I2C_Init(I2C1, &i2c_init);
    i2c_ready = true;
}

static bool TransferFailed(void)
{
    return I2C_GetFlagStatus(I2C1, I2C_FLAG_BERR) == SET ||
           I2C_GetFlagStatus(I2C1, I2C_FLAG_ARLO) == SET ||
           I2C_GetFlagStatus(I2C1, I2C_FLAG_AF) == SET ||
           I2C_GetFlagStatus(I2C1, I2C_FLAG_OVR) == SET ||
           I2C_GetFlagStatus(I2C1, I2C_FLAG_TIMEOUT) == SET;
}

static bool WaitTransferFlag(uint32_t flag, uint32_t deadline_ms)
{
    FlagStatus ready;

    do
    {
        ready = I2C_GetFlagStatus(I2C1, flag);
        if (TransferFailed() || !BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    } while (ready == RESET);
    return true;
}

''')
oled = replace(oled, '(I2C_SR2 & I2C_BUSY) != 0u', 'I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY) == SET')
oled = replace_region(oled, '    I2C_CR1 |= I2C_START;', '\n}\n\nbool OLED_I2C_Write',
    '''    I2C_GenerateSTART(I2C1, ENABLE);
    if (!WaitTransferFlag(I2C_FLAG_SB, deadline_ms))
    {
        return false;
    }
    I2C_Send7bitAddress(I2C1, (uint8_t)(RLC_OLED_I2C_ADDRESS << 1), I2C_Direction_Transmitter);
    if (!WaitTransferFlag(I2C_FLAG_ADDR, deadline_ms))
    {
        return false;
    }
    /* 确认 ADDR 后，使用 SPL 按 SR1、SR2 顺序读取，解除地址阶段时钟延展。 */
    (void)I2C_GetLastEvent(I2C1);
    return true;''')
oled = replace(oled, '(STM32_RCC_APB1ENR & I2C_APB1_ENABLE) == 0u', '!i2c_ready')
oled = replace(oled, '        I2C_DR = control;', '        I2C_SendData(I2C1, control);')
oled = replace(oled, '            I2C_DR = data[i];', '            I2C_SendData(I2C1, data[i]);')
oled = replace(oled, 'WaitEvent(I2C_TX_EMPTY, deadline_ms)', 'WaitTransferFlag(I2C_FLAG_TXE, deadline_ms)')
oled = replace(oled, 'WaitEvent(I2C_BYTE_SENT, deadline_ms)', 'WaitTransferFlag(I2C_FLAG_BTF, deadline_ms)')
oled = replace(oled, '    I2C_CR1 |= I2C_STOP;', '    I2C_GenerateSTOP(I2C1, ENABLE);')
oled = replace(oled, '        I2C_SR1 &= ~I2C_ERRORS;', '        I2C_ClearFlag(I2C1, OLED_I2C_ERROR_FLAGS);')
write(path, oled)
