/*
 * V1.9 大越 STM32H743VI M 版板级配置。排针号按 DevEBox 原理图。
 * ADC 的电压通道采 TP1 或 VDIFF，电流通道采 TP2。反馈阻容在 BSP_Board.c。
 * DDS 频率进 OLED 第 3 行，量程和硬件状态进第 4 行。
 */
#ifndef BSP_PINMAP_H
#define BSP_PINMAP_H

/* 25 MHz 晶振，PLL1 的 P、Q 输出 240、80 MHz。HCLK 120 MHz，APB 60 MHz。 */
#define RLC_SYSTEM_CLOCK_HZ 240000000u
#define RLC_APB_CLOCK_HZ     60000000u
#define RLC_TIM3_CLOCK_HZ   120000000u
#ifndef RLC_AD9833_USE_MCO
#define RLC_AD9833_USE_MCO          0
#endif
#ifndef RLC_AD9833_MCLK_HZ
#if RLC_AD9833_USE_MCO
#define RLC_AD9833_MCLK_HZ    8000000u /* PLL1Q / 10 */
#else
/* 使用板载振荡器时填入模块的实际 MCLK，PA8 留空。 */
#define RLC_AD9833_MCLK_HZ   25000000u
#endif

#endif /* RLC_AD9833_MCLK_HZ */

#define RLC_ADC_DUT_GPIO           GPIOA
#define RLC_ADC_DUT_PIN            LL_GPIO_PIN_6  /* J3-28，ADC1_INP3 */
#define RLC_ADC_DUT_CHANNEL        LL_ADC_CHANNEL_3
#define RLC_ADC_TIA_GPIO           GPIOC
#define RLC_ADC_TIA_PIN            LL_GPIO_PIN_4  /* J3-26，ADC2_INP4 */
#define RLC_ADC_TIA_CHANNEL        LL_ADC_CHANNEL_4
#define RLC_ADC_DUT_VOLTS_PER_CODE (3.3f / (65536.0f * 0.27675f))
#define RLC_ADC_TIA_VOLTS_PER_CODE (3.3f / (65536.0f * 0.27675f))
#define RLC_ADC_LOW_RAIL_CODE   32800u
#define RLC_ADC_HIGH_RAIL_CODE  52960u

#define RLC_DDS_GPIO        GPIOA
#define RLC_DDS_SELECT_PIN  LL_GPIO_PIN_4 /* J3-30 */
#define RLC_DDS_CLOCK_PIN   LL_GPIO_PIN_5 /* J3-29，SPI1 AF5 */
#define RLC_DDS_DATA_PIN    LL_GPIO_PIN_7 /* J3-27，SPI1 AF5 */
#define RLC_DDS_MCO_PIN     LL_GPIO_PIN_8 /* J2-20，MCO1 AF0 */

/* K0~K4 及 KCAL 各占一个输出。下拉设为 RF0 和限流。 */
#define RLC_RANGE_GPIO GPIOB
#define RLC_RELAY_K0_PIN   LL_GPIO_PIN_0  /* J3-24 */
#define RLC_RELAY_K1_PIN   LL_GPIO_PIN_1  /* J3-23 */
#define RLC_RELAY_K2_PIN   LL_GPIO_PIN_11 /* J3-11 */
#define RLC_RELAY_K3_PIN   LL_GPIO_PIN_12 /* J3-10 */
#define RLC_RELAY_K4_PIN   LL_GPIO_PIN_13 /* J3-9 */
#define RLC_RELAY_KCAL_PIN LL_GPIO_PIN_14 /* J3-8, High = NORMAL */
#define RLC_RANGE_SAFE_INDEX 0u
#define RLC_RANGE_SETTLE_MS 12u
#define RLC_DDS_OFF_SETTLE_MS  5u

#define RLC_OLED_GPIO          GPIOB
#define RLC_OLED_SCL_PIN       LL_GPIO_PIN_8  /* J2-42，I2C1 AF4 */
#define RLC_OLED_SDA_PIN       LL_GPIO_PIN_9  /* J2-41，I2C1 AF4 */
#define RLC_OLED_I2C_ADDRESS   0x3Cu
#define RLC_OLED_I2C_CLOCK_HZ  400000u
#define RLC_DEBUG_GPIO         GPIOA
#define RLC_DEBUG_TX_PIN       LL_GPIO_PIN_9  /* J2-19，USART1 AF7 */
#define RLC_DEBUG_BAUD         115200u
#define RLC_BUTTON_GPIO        GPIOB
#define RLC_BUTTON_PIN         LL_GPIO_PIN_10 /* J3-12，按下接 GND */

#endif
