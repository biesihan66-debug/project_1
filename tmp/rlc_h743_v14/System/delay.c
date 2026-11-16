/**
  ******************************************************************************
  * @file    delay.c
  * @author  铁头山羊
  * @version V 1.0.0
  * @date    2022年8月30日
  * @brief   延迟函数源文件
  ******************************************************************************
  */

/* 本项目在系统时钟就绪后启动 SysTick。ulTicks 每毫秒递增，GetTick() 供按键及超时判断。 */
#include "stm32h7xx.h"
#include "delay.h"

__IO uint32_t ulTicks;

/* 范例的 SysTick 回调在这里与计数器放在一起。 */
void SysTick_Handler(void)
{
    ulTicks++;
}

static uint8_t delay_initialized_flag = 0;
static uint32_t ticks_per_ms;


//
// @简介：初始化延迟函数
// @返回值：无
//
void Delay_Init(void)
{
	if(!delay_initialized_flag)
	{
		delay_initialized_flag = 1;
		
		ulTicks = 0;
		// SysTick 取内核时钟，SystemCoreClock 由 BSP_Clock_Init() 更新。
		ticks_per_ms = SystemCoreClock / 1000u;
		(void)SysTick_Config(ticks_per_ms);
		NVIC_SetPriority(SysTick_IRQn, 0u);
	}
}

//
// @简介：毫秒级延迟
// @参数：Delay - 延迟时长，以毫秒为单位(千分之一秒)
// @返回值：无
// @注意：不允许在中断响应函数中调用此方法
//
void Delay(uint32_t Delay)
{
	Delay_Init();
	
	uint32_t started = ulTicks;

	while ((uint32_t)(ulTicks - started) < Delay) {}
}

//
// @简介：获取当前时间，以毫秒（千分之一秒）为单位
// @参数：无
// @返回值：当前时间，单位为毫秒（千分之一秒）
//
uint32_t GetTick(void)
{
	Delay_Init();
	
	return ulTicks;
}

//
// @简介：获取当前的微秒级时间
// @返回值：当前的微秒级时间
//
/* 返回基于32位毫秒计数的时间，约49.7天回绕；延时使用模差值。 */
uint64_t GetUs(void)
{
    uint32_t tick, value, mask;
    Delay_Init();
    mask = __get_PRIMASK();
    __disable_irq();
    tick = ulTicks;
    value = SysTick->VAL;
    if ((SCB->ICSR & SCB_ICSR_PENDSTSET_Msk) != 0U)
    {
        tick++;
        value = SysTick->VAL;
    }
    __set_PRIMASK(mask);
    return (uint64_t)tick * 1000ULL
         + (uint64_t)(SysTick->LOAD - value) * 1000ULL / ticks_per_ms;
}

void DelayUs(uint32_t us)
{
    /* 分段避免UINT32_MAX请求和回绕相遇；额外1us防止时间量化导致短等。 */
    while (us != 0U)
    {
        uint32_t part = us > 1000000UL ? 1000000UL : us;
        uint32_t started = (uint32_t)GetUs();
        while ((uint32_t)((uint32_t)GetUs() - started) <= part) {}
        us -= part;
    }
}
