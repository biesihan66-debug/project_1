/*
 * V1.1 候选板级接线和测量参数。原理图定稿后统一核对本文件。
 * RLC_ADC_*_CHANNEL 对应 ADC1 电压和 ADC2 跨阻；RLC_RANGE_*_PIN
 * 为同一个 GPIO 端口上的三位档位控制。各 PIN 为 SPL 位掩码。
 * 板级驱动包含 stm32f10x.h 后使用这些宏。实际反馈阻容位于 BSP_Board.c。
 * DDS 频率最终在 OLED 第 3 行，反馈档位和硬件异常在第 4 行。
 */
#ifndef BSP_PINMAP_H
#define BSP_PINMAP_H

/* HSE 8 MHz 经 PLL×9，APB1 分频 2 后，TIM3 计数时钟仍为 72 MHz。 */
#define RLC_SYSTEM_CLOCK_HZ 72000000u
#define RLC_APB1_CLOCK_HZ   36000000u
#define RLC_TIM3_CLOCK_HZ   RLC_SYSTEM_CLOCK_HZ
#define RLC_AD9833_USE_MCO  1
#if RLC_AD9833_USE_MCO
#define RLC_AD9833_MCLK_HZ 8000000u
#else
/* 使用独立时钟时填写 DDS 实测 MCLK，避免解调参考取错。 */
#define RLC_AD9833_MCLK_HZ 25000000u
#endif

#define RLC_ADC_GPIO               GPIOA
#define RLC_ADC_DUT_PIN            GPIO_Pin_0
#define RLC_ADC_TIA_PIN            GPIO_Pin_1
#define RLC_ADC_DUT_CHANNEL        0u
#define RLC_ADC_TIA_CHANNEL        1u
#define RLC_ADC_DUT_VOLTS_PER_CODE (3.3f / 4096.0f)
#define RLC_ADC_TIA_VOLTS_PER_CODE (3.3f / 4096.0f)
#define RLC_ADC_LOW_RAIL_CODE      8u
#define RLC_ADC_HIGH_RAIL_CODE     4087u

/* SPI1 为 PA5/PA7，PA4 软件 FSYNC。PA8 的 MCO 可作 DDS MCLK。 */
#define RLC_DDS_GPIO       GPIOA
#define RLC_DDS_SELECT_PIN GPIO_Pin_4
#define RLC_DDS_CLOCK_PIN  GPIO_Pin_5
#define RLC_DDS_DATA_PIN   GPIO_Pin_7
#define RLC_DDS_MCO_PIN    GPIO_Pin_8

/* 三位译码器从 PB0、PB1、PB11 读取低、中、高位。硬件用下拉设上电安全档。 */
#define RLC_RANGE_GPIO       GPIOB
#define RLC_RANGE_BIT0_PIN   GPIO_Pin_0
#define RLC_RANGE_BIT1_PIN   GPIO_Pin_1
#define RLC_RANGE_BIT2_PIN   GPIO_Pin_11
#define RLC_RANGE_SAFE_INDEX 0u
#define RLC_RANGE_SETTLE_MS  4u

/* I2C1 使用 PB6/PB7 和 3.3 V 上拉；USART1 使用 PA9/PA10。 */
#define RLC_OLED_GPIO         GPIOB
#define RLC_OLED_SCL_PIN      GPIO_Pin_6
#define RLC_OLED_SDA_PIN      GPIO_Pin_7
#define RLC_OLED_I2C_ADDRESS  0x3Cu
#define RLC_OLED_I2C_CLOCK_HZ 400000u
#define RLC_DEBUG_GPIO        GPIOA
#define RLC_DEBUG_TX_PIN      GPIO_Pin_9
#define RLC_DEBUG_RX_PIN      GPIO_Pin_10
#define RLC_DEBUG_BAUD        115200u
#define RLC_BUTTON_GPIO       GPIOB
#define RLC_BUTTON_PIN        GPIO_Pin_10

/* PA13、PA14 和 NRST 预留给 ST-LINK 的 SWD 调试与复位。 */
#define RLC_FLASH_PAGE_BYTES   1024u
#define RLC_CALIBRATION_PAGE_A 0x0800F800u
#define RLC_CALIBRATION_PAGE_B 0x0800FC00u

#endif
