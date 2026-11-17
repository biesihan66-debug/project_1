/*
 * 大越 M 版时钟。25 MHz HSE / 5 × 96 产生 480 MHz PLL1 VCO。
 * P / 2 供 240 MHz 内核，Q / 6 供 SPI1 和 MCO1。总线分频见 BSP_PinMap.h。
 * SystemCoreClock 由 CMSIS 更新，铁头山羊 Delay 据此产生 1 ms SysTick。
 * 本模块不直接更新 OLED。
 */
#include "BSP_Clock.h"
#include "BSP_PinMap.h"
#include "stm32h7xx_ll_pwr.h"
#include "stm32h7xx_ll_rcc.h"

/* SysTick 启用前用有限次数轮询，避免晶振或 PLL 异常时挂在初始化中。 */
#define CLOCK_WAIT_ATTEMPTS 2000000u

static bool WaitForClock(uint32_t (*read_status)(void), uint32_t expected)
{
    for (uint32_t remaining = CLOCK_WAIT_ATTEMPTS; remaining != 0u; --remaining)
    {
        if (read_status() == expected)
        {
            return true;
        }
    }
    return false;
}

bool BSP_Clock_Init(void)
{
    /* 启动文件已在使用 SRAM 前切至 LDO，并等待稳压。 */
    LL_PWR_SetRegulVoltageScaling(LL_PWR_REGU_VOLTAGE_SCALE1);
    if (!WaitForClock(LL_PWR_IsActiveFlag_VOS, 1u))
    {
        return false;
    }

    LL_RCC_HSE_DisableBypass();
    LL_RCC_HSE_Enable();
    if (!WaitForClock(LL_RCC_HSE_IsReady, 1u))
    {
        return false;
    }

    /* Flash ACR 通过 CMSIS 设置。先提高等待周期，再提高 HCLK。 */
    MODIFY_REG(FLASH->ACR, FLASH_ACR_LATENCY | FLASH_ACR_WRHIGHFREQ,
               FLASH_ACR_LATENCY_2WS | FLASH_ACR_WRHIGHFREQ_0);
    if ((FLASH->ACR & FLASH_ACR_LATENCY) != FLASH_ACR_LATENCY_2WS)
    {
        return false;
    }

    LL_RCC_PLL_SetSource(LL_RCC_PLLSOURCE_HSE);
    LL_RCC_PLL1FRACN_Disable();
    LL_RCC_PLL1_SetVCOInputRange(LL_RCC_PLLINPUTRANGE_4_8);
    LL_RCC_PLL1_SetVCOOutputRange(LL_RCC_PLLVCORANGE_WIDE);
    LL_RCC_PLL1_SetM(5u);
    LL_RCC_PLL1_SetN(96u);
    LL_RCC_PLL1_SetP(2u);
    LL_RCC_PLL1_SetQ(6u);
    LL_RCC_PLL1P_Enable();
    LL_RCC_PLL1Q_Enable();
    LL_RCC_PLL1_Enable();
    if (!WaitForClock(LL_RCC_PLL1_IsReady, 1u))
    {
        return false;
    }

    LL_RCC_SetSysPrescaler(LL_RCC_SYSCLK_DIV_1);
    LL_RCC_SetAHBPrescaler(LL_RCC_AHB_DIV_2);
    LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2);
    LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_2);
    LL_RCC_SetAPB3Prescaler(LL_RCC_APB3_DIV_2);
    LL_RCC_SetAPB4Prescaler(LL_RCC_APB4_DIV_2);
    LL_RCC_SetTIMPrescaler(LL_RCC_TIM_PRESCALER_TWICE);
    LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL1);
    if (!WaitForClock(LL_RCC_GetSysClkSource, LL_RCC_SYS_CLKSOURCE_STATUS_PLL1))
    {
        return false;
    }
    SystemCoreClockUpdate();
    return SystemCoreClock == RLC_SYSTEM_CLOCK_HZ;
}
