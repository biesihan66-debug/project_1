/*
 * STM32F103C8T6 寄存器位置与 GPIO 基本操作，地址来自 ST RM0008。
 * BASE 常量为片上地址，REG32/REG16 访问对应宽度。GPIO 配置位于 CRL/CRH。
 * 采样、换档及显示传输使用本文件，本文件的寄存器数据不直接进入 OLED。
 */
#ifndef STM32F103_REGISTERS_H
#define STM32F103_REGISTERS_H

#include <stdbool.h>
#include <stdint.h>

#define STM32_REG32(address) (*(volatile uint32_t *)(uintptr_t)(address))
#define STM32_REG16(address) (*(volatile uint16_t *)(uintptr_t)(address))
#define STM32_RCC            0x40021000u
#define STM32_FLASH          0x40022000u
#define STM32_GPIOA          0x40010800u
#define STM32_GPIOB          0x40010C00u
#define STM32_DMA1           0x40020000u
#define STM32_ADC1           0x40012400u
#define STM32_ADC2           0x40012800u
#define STM32_TIM3           0x40000400u
#define STM32_SPI1           0x40013000u
#define STM32_USART1         0x40013800u
#define STM32_I2C1           0x40005400u
#define STM32_SYSTICK        0xE000E010u

#define STM32_RCC_CR         STM32_REG32(STM32_RCC + 0x00u)
#define STM32_RCC_CFGR       STM32_REG32(STM32_RCC + 0x04u)
#define STM32_RCC_APB2RSTR   STM32_REG32(STM32_RCC + 0x0Cu)
#define STM32_RCC_APB1RSTR   STM32_REG32(STM32_RCC + 0x10u)
#define STM32_RCC_AHBENR     STM32_REG32(STM32_RCC + 0x14u)
#define STM32_RCC_APB2ENR    STM32_REG32(STM32_RCC + 0x18u)
#define STM32_RCC_APB1ENR    STM32_REG32(STM32_RCC + 0x1Cu)

#define STM32_GPIO_ANALOG_IN 0x0u
#define STM32_GPIO_FLOAT_IN  0x4u
#define STM32_GPIO_PULL_IN   0x8u
#define STM32_GPIO_OUT_PP    0x2u
#define STM32_GPIO_AF_PP     0xBu
#define STM32_GPIO_AF_OD     0xFu

static inline void stm32_gpio_configure(uint32_t gpio, uint8_t pin, uint32_t mode)
{
    uint32_t offset = pin < 8u ? 0u : 4u;
    uint32_t shift = (uint32_t)(pin % 8u) * 4u;
    volatile uint32_t *config = &STM32_REG32(gpio + offset);

    *config = (*config & ~(0xFu << shift)) | (mode << shift);
}

static inline void stm32_gpio_set(uint32_t gpio, uint8_t pin, bool high)
{
    uint32_t shift = (uint32_t)pin + (high ? 0u : 16u);

    STM32_REG32(gpio + 0x10u) = 1u << shift;
}

static inline bool stm32_gpio_is_high(uint32_t gpio, uint8_t pin)
{
    return (STM32_REG32(gpio + 0x08u) & (1u << pin)) != 0u;
}

#endif
