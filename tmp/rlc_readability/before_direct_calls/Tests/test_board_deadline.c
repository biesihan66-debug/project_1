/*
 * 板级选档的主机测试。通过 BSP_GetMeasureHardware() 调用实际选档逻辑。
 * DDS 替身调用 SysTick_Handler() 推进真实板级计数，模拟 SPI 传输碰到截止时刻。
 * 测试保持在安全档，不初始化外设、不访问硬件寄存器。
 */
#include "BSP_PinMap.h"
#include "BSP_Board.h"
#include "DDS_Signal.h"
#include "ADC_Sample.h"
#include "Range_Switch.h"
#include "OLED_I2C.h"

#include <stdio.h>
#include <stdlib.h>

const uint32_t rlc_frequency_hz[RLC_FREQUENCY_COUNT] = {100u, 1000u, 10000u, 100000u};

void SysTick_Handler(void);

static bool expire_during_reset;
static bool expire_during_start;

void Range_Init(void) {}
void OLED_I2C_Init(void) {}

bool DDS_Init(void)
{
    return true;
}

bool ADC_Init(void)
{
    return true;
}

void Range_Set(uint8_t range_index)
{
    (void)range_index;
}

bool DDS_Stop(uint32_t deadline_ms)
{
    if (expire_during_reset)
    {
        SysTick_Handler();
    }
    return BSP_BeforeDeadline(deadline_ms);
}

bool DDS_SetFrequency(uint32_t nominal_hz, uint32_t deadline_ms)
{
    (void)nominal_hz;
    if (expire_during_start)
    {
        SysTick_Handler();
    }
    return BSP_BeforeDeadline(deadline_ms);
}

float DDS_GetActualFrequencyHz(void)
{
    return 1000.0f;
}

RLC_Status ADC_Capture(uint32_t nominal_hz, float actual_hz, uint8_t cycles, uint32_t deadline_ms,
                       RLC_Frame *frame)
{
    (void)nominal_hz;
    (void)actual_hz;
    (void)cycles;
    (void)deadline_ms;
    (void)frame;
    return RLC_STATUS_HARDWARE_ERROR;
}

static void check_selection(const RLC_Hardware *port, uint8_t frequency_index, uint8_t range_index,
                            uint32_t remaining_ms, RLC_Status expected, const char *description)
{
    const uint32_t deadline_ms = port->millis(port->context) + remaining_ms;
    const RLC_Status actual =
        port->select_setting(port->context, frequency_index, range_index, deadline_ms);
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (status %d, expected %d)\n", description, (int)actual,
                (int)expected);
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    const RLC_Hardware *port = BSP_GetMeasureHardware();
    check_selection(port, RLC_FREQUENCY_COUNT, RLC_RANGE_SAFE_INDEX, 1u, RLC_STATUS_HARDWARE_ERROR,
                    "invalid frequency index");
    check_selection(port, RLC_FREQUENCY_1_KHZ, RLC_RANGE_COUNT, 1u, RLC_STATUS_HARDWARE_ERROR,
                    "invalid range index");
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
