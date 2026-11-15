#include "stm32f10x.h"
#include "delay.h"

#include <assert.h>
#include <stdio.h>

TestSysTick test_systick;
TestSCB test_scb;
static unsigned clock_reads;
static uint32_t irq_mask;

void RCC_GetClocksFreq(RCC_ClocksTypeDef *clocks)
{
    ++clock_reads;
    clocks->HCLK_Frequency = 72000000u;
}

uint32_t __get_PRIMASK(void) { return irq_mask; }
void __disable_irq(void) { irq_mask = 1u; }
void __set_PRIMASK(uint32_t mask) { irq_mask = mask; }
void SysTick_Handler(void);

int main(void)
{
    test_scb.SHP[11] = 0xf0u;
    Delay_Init();
    assert(test_systick.LOAD == 71999u && test_systick.VAL == 0u);
    assert(test_systick.CTRL == 7u && test_scb.SHP[11] == 0u);
    for (unsigned i = 0u; i < 100u; ++i)
    {
        SysTick_Handler();
    }
    test_systick.VAL = 35999u;
    assert(GetTick() == 100u && clock_reads == 1u && test_systick.VAL == 35999u);
    assert(GetUs() == 100500u && irq_mask == 0u);
    test_scb.ICSR = SCB_ICSR_PENDSTSET_Msk;
    irq_mask = 1u;
    assert(GetUs() == 101500u && irq_mask == 1u);

    ulTicks = UINT32_MAX;
    SysTick_Handler();
    assert(GetTick() == 0u && clock_reads == 1u);
    Delay(0u);
    puts("PASS: project Delay_Init/GetTick/SysTick at 72 MHz, repeated reads and wrap");
    return 0;
}
