/*
 * STM32 板级公开接口。pin/ADC/定时器映射由 board_pin_map.h 管理。
 * 板级计时用于消抖和测量超时，Flash 接口接收应用打包的校准记录。
 * OLED 数据经 rlc_board_oled_write() 写入，行号由 oled_result_display.c 定义。
 */
#ifndef STM32_RLC_BOARD_H
#define STM32_RLC_BOARD_H

#include "rlc_measurement_port.h"

bool rlc_board_initialize(void);
const RlcMeasurementPort *rlc_board_measurement_port(void);
uint32_t rlc_board_millis(void);
bool rlc_board_button_pressed(void);
void rlc_board_debug_write(const char *text);
bool rlc_board_oled_write(uint8_t control, const uint8_t *data, uint16_t count);
bool rlc_board_load_calibration(void *data, size_t size);
bool rlc_board_save_calibration(const void *data, size_t size);

#endif
