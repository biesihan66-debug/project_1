/*
 * 板级选档的主机测试。通过 rlc_board_measurement_port() 调用实际选档逻辑。
 * DDS 替身调用 SysTick_Handler() 推进真实板级计数，模拟 SPI 传输碰到截止时刻。
 * 测试保持在安全档，不初始化外设、不访问硬件寄存器。
 */
#include "board_pin_map.h"
#include "rlc_board_internal.h"

#include <stdio.h>
#include <stdlib.h>

const uint32_t rlc_frequency_hz[RLC_FREQUENCY_COUNT] = {100u, 1000u, 10000u, 100000u};

void SysTick_Handler(void);

static bool expire_during_reset;
static bool expire_during_start;

void rlc_board_range_initialize(void) {}
void rlc_board_oled_bus_initialize(void) {}

bool rlc_board_excitation_initialize(void)
{
    return true;
}

bool rlc_board_sampling_initialize(void)
{
    return true;
}

void rlc_board_range_set(uint8_t range_index)
{
    (void)range_index;
}

bool rlc_board_excitation_reset(uint32_t deadline_ms)
{
    if (expire_during_reset) {
        SysTick_Handler();
    }
    return rlc_board_before_deadline(deadline_ms);
}

bool rlc_board_excitation_start(uint32_t nominal_hz, uint32_t deadline_ms)
{
    (void)nominal_hz;
    if (expire_during_start) {
        SysTick_Handler();
    }
    return rlc_board_before_deadline(deadline_ms);
}

float rlc_board_excitation_actual_hz(void)
{
    return 1000.0f;
}

RlcStatus rlc_board_sample_frame(uint32_t nominal_hz, float actual_hz, uint8_t cycles,
                                uint32_t deadline_ms, RlcSampleFrame *frame)
{
    (void)nominal_hz;
    (void)actual_hz;
    (void)cycles;
    (void)deadline_ms;
    (void)frame;
    return RLC_STATUS_HARDWARE_ERROR;
}

static void check_selection(const RlcMeasurementPort *port, uint8_t frequency_index,
                            uint8_t range_index, uint32_t remaining_ms,
                            RlcStatus expected, const char *description)
{
    const uint32_t deadline_ms = port->millis(port->context) + remaining_ms;
    const RlcStatus actual = port->select_setting(port->context, frequency_index,
                                                  range_index, deadline_ms);
    if (actual != expected) {
        fprintf(stderr, "FAIL: %s (status %d, expected %d)\n",
                description, (int)actual, (int)expected);
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    const RlcMeasurementPort *port = rlc_board_measurement_port();
    check_selection(port, RLC_FREQUENCY_COUNT, RLC_RANGE_SAFE_INDEX, 1u,
                    RLC_STATUS_HARDWARE_ERROR,
                    "invalid frequency index");
    check_selection(port, RLC_FREQUENCY_1_KHZ, RLC_RANGE_COUNT, 1u,
                    RLC_STATUS_HARDWARE_ERROR, "invalid range index");
    check_selection(port, RLC_FREQUENCY_1_KHZ, RLC_RANGE_SAFE_INDEX, 0u, RLC_STATUS_TIMEOUT,
                    "deadline already expired");

    expire_during_reset = true;
    check_selection(port, RLC_FREQUENCY_1_KHZ, RLC_RANGE_SAFE_INDEX, 1u, RLC_STATUS_TIMEOUT,
                    "deadline expires during DDS reset");
    expire_during_reset = false;
    expire_during_start = true;
    check_selection(port, RLC_FREQUENCY_1_KHZ, RLC_RANGE_SAFE_INDEX, 1u, RLC_STATUS_TIMEOUT,
                    "deadline expires during DDS start");
    puts("PASS: board setting deadlines and index validation");
    return EXIT_SUCCESS;
}
