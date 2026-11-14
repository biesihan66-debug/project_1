/*
 * OLED 的 I2C1 主发送。SCL/SDA 和 7 位地址在 BSP_PinMap.h。
 * control 是控制字节，data[i] 按序发送，count 为数据字节数。
 * i2c_ready 记录 I2C1 是否初始化；deadline_ms 是本次总线操作的截止时刻。
 * 屏幕行布局由 OLED_Display.c 管理，传输层只发送命令或像素。
 */
#include "stm32f10x.h"
#include "BSP_PinMap.h"
#include "OLED_I2C.h"
#include "BSP_Board.h"

#define OLED_I2C_ERROR_FLAGS (I2C_FLAG_BERR | I2C_FLAG_ARLO | I2C_FLAG_AF | \
                             I2C_FLAG_OVR | I2C_FLAG_TIMEOUT)

static bool i2c_ready;

static bool WaitBusPin(uint16_t pin, bool high, uint32_t deadline_ms)
{
    while ((GPIO_ReadInputDataBit(RLC_OLED_GPIO, pin) == Bit_SET) != high)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    }
    return true;
}

static bool SetBusPin(uint16_t pin, bool high, uint32_t deadline_ms)
{
    GPIO_WriteBit(RLC_OLED_GPIO, pin, high ? Bit_SET : Bit_RESET);
    return WaitBusPin(pin, high, deadline_ms);
}

static void ReleaseI2CAnalogFilters(void)
{
    const uint32_t deadline_ms = BSP_GetMillis() + 3u;
    GPIO_InitTypeDef gpio_init;

    /* ES096 §2.8.7：仅复位 I2C 无法解除模拟滤波器造成的 BUSY 锁定。 */
    I2C_Cmd(I2C1, DISABLE);
    GPIO_SetBits(RLC_OLED_GPIO, RLC_OLED_SCL_PIN | RLC_OLED_SDA_PIN);
    gpio_init.GPIO_Pin = RLC_OLED_SCL_PIN | RLC_OLED_SDA_PIN;
    gpio_init.GPIO_Mode = GPIO_Mode_Out_OD;
    gpio_init.GPIO_Speed = GPIO_Speed_2MHz;
    GPIO_Init(RLC_OLED_GPIO, &gpio_init);

    /* 按原厂顺序翻转两线，并用 IDR 核对每一步。外部拉低时不一直等待。 */
    if (WaitBusPin(RLC_OLED_SCL_PIN, true, deadline_ms) &&
        WaitBusPin(RLC_OLED_SDA_PIN, true, deadline_ms) &&
        SetBusPin(RLC_OLED_SDA_PIN, false, deadline_ms) &&
        SetBusPin(RLC_OLED_SCL_PIN, false, deadline_ms) &&
        SetBusPin(RLC_OLED_SCL_PIN, true, deadline_ms))
    {
        (void)SetBusPin(RLC_OLED_SDA_PIN, true, deadline_ms);
    }
    GPIO_SetBits(RLC_OLED_GPIO, RLC_OLED_SCL_PIN | RLC_OLED_SDA_PIN);
    gpio_init.GPIO_Mode = GPIO_Mode_AF_OD;
    gpio_init.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(RLC_OLED_GPIO, &gpio_init);
    I2C_SoftwareResetCmd(I2C1, ENABLE);
    I2C_SoftwareResetCmd(I2C1, DISABLE);
}

void OLED_I2C_Init(void)
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

static bool StartDisplayTransfer(uint32_t deadline_ms)
{
    while (I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY) == SET)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    }
    I2C_GenerateSTART(I2C1, ENABLE);
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
    return true;
}

bool OLED_I2C_Write(uint8_t control, const uint8_t *data, uint16_t count)
{
    uint32_t deadline_ms;
    uint16_t i;
    bool sent;

    if (!i2c_ready || count == 0u || data == NULL)
    {
        return false;
    }
    deadline_ms = BSP_GetMillis() + 5u + ((uint32_t)count + 2u) * 9000u / RLC_OLED_I2C_CLOCK_HZ;
    sent = StartDisplayTransfer(deadline_ms);
    if (sent)
    {
        I2C_SendData(I2C1, control);
        for (i = 0u; i < count; ++i)
        {
            if (!WaitTransferFlag(I2C_FLAG_TXE, deadline_ms))
            {
                sent = false;
                break;
            }
            I2C_SendData(I2C1, data[i]);
        }
    }
    if (sent)
    {
        sent = WaitTransferFlag(I2C_FLAG_BTF, deadline_ms);
    }

    I2C_GenerateSTOP(I2C1, ENABLE);
    if (!sent)
    {
        I2C_ClearFlag(I2C1, OLED_I2C_ERROR_FLAGS);
        OLED_I2C_Init();
    }
    return sent;
}
