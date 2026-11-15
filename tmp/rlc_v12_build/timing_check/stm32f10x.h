#include <stdint.h>

#define __IO volatile
#define SysTick_CTRL_ENABLE 1u
#define SysTick_CTRL_TICKINT 2u
#define SysTick_CTRL_CLKSOURCE 4u
#define SCB_ICSR_PENDSTSET_Msk (1u << 26)

typedef struct {
    volatile uint32_t CTRL, LOAD, VAL;
} TestSysTick;

typedef struct {
    volatile uint32_t ICSR;
    volatile uint8_t SHP[12];
} TestSCB;

typedef struct {
    uint32_t SYSCLK_Frequency, HCLK_Frequency, PCLK1_Frequency, PCLK2_Frequency, ADCCLK_Frequency;
} RCC_ClocksTypeDef;

extern TestSysTick test_systick;
extern TestSCB test_scb;
#define SysTick (&test_systick)
#define SCB (&test_scb)

void RCC_GetClocksFreq(RCC_ClocksTypeDef *clocks);
uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
