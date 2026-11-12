/*
 * STM32 板级内部调用约定。所有 deadline_ms 均取自 rlc_board_millis，
 * 一次等待的时间远小于半个 32 位计数范围。采样数组由采样模块维护。
 * 频率对应 OLED 第 3 行，档位及板级错误对应第 4 行。
 */
#ifndef RLC_BOARD_INTERNAL_H
#define RLC_BOARD_INTERNAL_H

#include "stm32_rlc_board.h"

bool rlc_board_before_deadline(uint32_t deadline_ms);
void rlc_board_oled_bus_initialize(void);
void rlc_board_range_initialize(void);
void rlc_board_range_set(uint8_t range_index);
bool rlc_board_excitation_initialize(void);
bool rlc_board_excitation_reset(uint32_t deadline_ms);
bool rlc_board_excitation_start(uint32_t nominal_hz, uint32_t deadline_ms);
float rlc_board_excitation_actual_hz(void);
bool rlc_board_sampling_initialize(void);
RlcStatus rlc_board_sample_frame(uint32_t nominal_hz, float actual_hz,
                                uint8_t cycles, uint32_t deadline_ms,
                                RlcSampleFrame *frame);

#endif
