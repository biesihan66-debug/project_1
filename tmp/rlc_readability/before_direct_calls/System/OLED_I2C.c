/*
 * OLED 的 I2C1 主发送。SCL/SDA 和 7 位地址在 BSP_PinMap.h。
 * control 是控制字节，data[i] 按序发送，count 为数据字节数。
 * 屏幕行布局由 OLED_Display.c 管理，传输层只发送命令或像素。
 */
#include "BSP_PinMap.h"
#include "OLED_I2C.h"
#include "BSP_Board.h"

#define I2C_CR1   STM32_REG32(STM32_I2C1 + 0x00u)
#define I2C_CR2   STM32_REG32(STM32_I2C1 + 0x04u)
#define I2C_OAR1  STM32_REG32(STM32_I2C1 + 0x08u)
#define I2C_DR    STM32_REG32(STM32_I2C1 + 0x10u)
#define I2C_SR1   STM32_REG32(STM32_I2C1 + 0x14u)
#define I2C_SR2   STM32_REG32(STM32_I2C1 + 0x18u)
#define I2C_CCR   STM32_REG32(STM32_I2C1 + 0x1Cu)
#define I2C_TRISE STM32_REG32(STM32_I2C1 + 0x20u)

#define I2C_APB1_ENABLE  (1u << 21)
#define I2C_ENABLE       1u
#define I2C_START        (1u << 8)
#define I2C_STOP         (1u << 9)
#define I2C_SOFT_RESET   (1u << 15)
#define I2C_START_SENT   1u
#define I2C_ADDRESS_SENT (1u << 1)
#define I2C_BYTE_SENT    (1u << 2)
#define I2C_TX_EMPTY     (1u << 7)
#define I2C_ERRORS       0x4F00u
#define I2C_BUSY         (1u << 1)

static bool WaitBusPin(uint8_t pin, bool high, uint32_t deadline_ms)
{
    while (BSP_GPIO_Read(RLC_OLED_GPIO, pin) != high)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    }
    return true;
}

static bool SetBusPin(uint8_t pin, bool high, uint32_t deadline_ms)
{
    BSP_GPIO_Write(RLC_OLED_GPIO, pin, high);
    return WaitBusPin(pin, high, deadline_ms);
}

static void ReleaseI2CAnalogFilters(void)
{
    const uint32_t deadline_ms = BSP_GetMillis() + 3u;
    const uint32_t output_open_drain = 0x6u; /* 通用开漏输出，2 MHz。 */

    /* ES096 §2.8.7：仅复位 I2C 无法解除模拟滤波器造成的 BUSY 锁定。 */
    I2C_CR1 = 0u;
    BSP_GPIO_Write(RLC_OLED_GPIO, RLC_OLED_SCL_PIN, true);
    BSP_GPIO_Write(RLC_OLED_GPIO, RLC_OLED_SDA_PIN, true);
    BSP_GPIO_Config(RLC_OLED_GPIO, RLC_OLED_SCL_PIN, output_open_drain);
    BSP_GPIO_Config(RLC_OLED_GPIO, RLC_OLED_SDA_PIN, output_open_drain);

    /* 按原厂顺序翻转两线，并用 IDR 核对每一步。外部拉低时不一直等待。 */
    if (WaitBusPin(RLC_OLED_SCL_PIN, true, deadline_ms) &&
        WaitBusPin(RLC_OLED_SDA_PIN, true, deadline_ms) &&
        SetBusPin(RLC_OLED_SDA_PIN, false, deadline_ms) &&
        SetBusPin(RLC_OLED_SCL_PIN, false, deadline_ms) &&
        SetBusPin(RLC_OLED_SCL_PIN, true, deadline_ms))
    {
        (void)SetBusPin(RLC_OLED_SDA_PIN, true, deadline_ms);
    }
    BSP_GPIO_Write(RLC_OLED_GPIO, RLC_OLED_SCL_PIN, true);
    BSP_GPIO_Write(RLC_OLED_GPIO, RLC_OLED_SDA_PIN, true);
    BSP_GPIO_Config(RLC_OLED_GPIO, RLC_OLED_SCL_PIN, STM32_GPIO_AF_OD);
    BSP_GPIO_Config(RLC_OLED_GPIO, RLC_OLED_SDA_PIN, STM32_GPIO_AF_OD);
    I2C_CR1 = I2C_SOFT_RESET;
    I2C_CR1 = 0u;
}

void OLED_I2C_Init(void)
{
    STM32_RCC_APB1ENR |= I2C_APB1_ENABLE;
    STM32_RCC_APB1RSTR |= I2C_APB1_ENABLE;
    STM32_RCC_APB1RSTR &= ~I2C_APB1_ENABLE;
    ReleaseI2CAnalogFilters();

    I2C_CR2 = RLC_APB1_CLOCK_HZ / 1000000u;
    I2C_OAR1 = 1u << 14;
#if RLC_OLED_I2C_CLOCK_HZ <= 100000u
    I2C_CCR = RLC_APB1_CLOCK_HZ / (2u * RLC_OLED_I2C_CLOCK_HZ);
    I2C_TRISE = RLC_APB1_CLOCK_HZ / 1000000u + 1u;
#else
    I2C_CCR = (1u << 15) | (RLC_APB1_CLOCK_HZ / (3u * RLC_OLED_I2C_CLOCK_HZ));
    I2C_TRISE = RLC_APB1_CLOCK_HZ / 1000000u * 3u / 10u + 1u;
#endif
    I2C_CR1 = I2C_ENABLE;
}

static bool WaitEvent(uint32_t event, uint32_t deadline_ms)
{
    uint32_t status;

    do
    {
        status = I2C_SR1;
        if ((status & I2C_ERRORS) != 0u || !BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    } while ((status & event) == 0u);
    return true;
}

static bool StartDisplayTransfer(uint32_t deadline_ms)
{
    while ((I2C_SR2 & I2C_BUSY) != 0u)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    }
    I2C_CR1 |= I2C_START;
    if (!WaitEvent(I2C_START_SENT, deadline_ms))
    {
        return false;
    }
    I2C_DR = RLC_OLED_I2C_ADDRESS << 1;
    if (!WaitEvent(I2C_ADDRESS_SENT, deadline_ms))
    {
        return false;
    }
    (void)I2C_SR1;
    (void)I2C_SR2; /* 先读 SR1，再读 SR2，以清除 ADDR。 */
    return true;
}

bool OLED_I2C_Write(uint8_t control, const uint8_t *data, uint16_t count)
{
    uint32_t deadline_ms;
    uint16_t i;
    bool sent;

    if ((STM32_RCC_APB1ENR & I2C_APB1_ENABLE) == 0u || count == 0u || data == NULL)
    {
        return false;
    }
    deadline_ms = BSP_GetMillis() + 5u + ((uint32_t)count + 2u) * 9000u / RLC_OLED_I2C_CLOCK_HZ;
    sent = StartDisplayTransfer(deadline_ms);
    if (sent)
    {
        I2C_DR = control;
        for (i = 0u; i < count; ++i)
        {
            if (!WaitEvent(I2C_TX_EMPTY, deadline_ms))
            {
                sent = false;
                break;
            }
            I2C_DR = data[i];
        }
    }
    if (sent)
    {
        sent = WaitEvent(I2C_BYTE_SENT, deadline_ms);
    }

    I2C_CR1 |= I2C_STOP;
    if (!sent)
    {
        I2C_SR1 &= ~I2C_ERRORS;
        OLED_I2C_Init();
    }
    return sent;
}
