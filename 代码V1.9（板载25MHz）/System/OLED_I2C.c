/*
 * SSD1306 的 I2C1 发送。PB8/SCL、PB9/SDA 接至 OLED，使用 3.3 V 上拉。
 * control 是控制字节，data[i] 从 0 开始按序发送，count 为有效数据字节数。
 * deadline_ms 由铁头山羊 GetTick() 推算，限制一次整批传输的总耗时。
 * 本文件只送命令和图像，OLED_Display.c 管理第 1～4 行显示内容。
 */
#include "OLED_I2C.h"
#include "BSP_PinMap.h"
#include "BSP_Board.h"
#include "stm32h7xx_ll_bus.h"
#include "stm32h7xx_ll_gpio.h"
#include "stm32h7xx_ll_i2c.h"
#include "stm32h7xx_ll_rcc.h"

/* 60 MHz PCLK1，Fast-mode。PRESC=2、SCLDEL=9、SDADEL=4、SCLH=20、SCLL=25。 */
#define OLED_I2C_TIMING 0x20941419u
#define OLED_I2C_ERRORS (I2C_ISR_NACKF | I2C_ISR_ARLO | I2C_ISR_OVR)

static bool i2c_ready;

static void ResetI2C(void)
{
    LL_APB1_GRP1_ForceReset(LL_APB1_GRP1_PERIPH_I2C1);
    LL_APB1_GRP1_ReleaseReset(LL_APB1_GRP1_PERIPH_I2C1);
    LL_I2C_SetTiming(I2C1, OLED_I2C_TIMING);
    LL_I2C_Enable(I2C1);
}

void OLED_I2C_Init(void)
{
    LL_GPIO_InitTypeDef gpio = {0};

    gpio.Pin = RLC_OLED_SCL_PIN | RLC_OLED_SDA_PIN;
    gpio.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
    gpio.Speed = LL_GPIO_SPEED_FREQ_HIGH;
    gpio.Alternate = LL_GPIO_AF_4;
    LL_GPIO_Init(RLC_OLED_GPIO, &gpio);

    LL_RCC_SetI2CClockSource(LL_RCC_I2C123_CLKSOURCE_PCLK1);
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_I2C1);
    ResetI2C();
    i2c_ready = true;
}

static bool WaitTransferFlag(uint32_t flag, uint32_t deadline_ms)
{
    while (BSP_BeforeDeadline(deadline_ms))
    {
        /* 一次读取同时检查发送进度和总线错误。 */
        const uint32_t status = LL_I2C_ReadReg(I2C1, ISR);
        if ((status & I2C_ISR_BERR) != 0u)
        {
            /* ES0392 §2.19.4：控制器传输继续，清除误报即可。 */
            LL_I2C_ClearFlag_BERR(I2C1);
        }
        if ((status & OLED_I2C_ERRORS) != 0u)
        {
            return false;
        }
        if ((status & flag) != 0u)
        {
            return true;
        }
    }
    return false;
}

static bool WriteDisplayBytes(uint8_t control, const uint8_t *data, uint16_t count,
                              uint32_t deadline_ms)
{
    uint32_t remaining = (uint32_t)count + 1u;
    uint32_t request = LL_I2C_GENERATE_START_WRITE;
    while (LL_I2C_IsActiveFlag_BUSY(I2C1) != 0u)
    {
        if (!BSP_BeforeDeadline(deadline_ms))
        {
            return false;
        }
    }

    /* 单次 NBYTES 最多 255 字节。首块含控制字节，最后一块由硬件发 STOP。 */
    for (uint32_t offset = 0u; remaining != 0u; request = LL_I2C_GENERATE_NOSTARTSTOP)
    {
        const uint32_t block_size = remaining > 255u ? 255u : remaining;
        remaining -= block_size;
        LL_I2C_HandleTransfer(I2C1, RLC_OLED_I2C_ADDRESS << 1, LL_I2C_ADDRSLAVE_7BIT,
                              block_size, remaining != 0u ? LL_I2C_MODE_RELOAD : LL_I2C_MODE_AUTOEND,
                              request);
        for (uint32_t i = 0u; i < block_size; ++i, ++offset)
        {
            if (!WaitTransferFlag(I2C_ISR_TXIS, deadline_ms))
            {
                return false;
            }
            LL_I2C_TransmitData8(I2C1, offset == 0u ? control : data[offset - 1u]);
        }
        if (remaining != 0u && !WaitTransferFlag(I2C_ISR_TCR, deadline_ms))
        {
            return false;
        }
    }
    return WaitTransferFlag(I2C_ISR_STOPF, deadline_ms);
}

bool OLED_I2C_Write(uint8_t control, const uint8_t *data, uint16_t count)
{
    if (!i2c_ready || count == 0u || data == NULL)
    {
        return false;
    }
    const uint32_t deadline_ms = BSP_GetMillis() + 5u +
                                ((uint32_t)count + 2u) * 9000u / RLC_OLED_I2C_CLOCK_HZ;
    if (!WriteDisplayBytes(control, data, count, deadline_ms))
    {
        ResetI2C();
        return false;
    }
    LL_I2C_ClearFlag_STOP(I2C1);
    return true;
}
