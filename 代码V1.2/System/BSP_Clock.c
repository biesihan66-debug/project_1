/*
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
