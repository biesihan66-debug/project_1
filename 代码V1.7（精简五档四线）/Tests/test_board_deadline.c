/*
 * 板级选档的主机测试。直接调用 BSP_SelectMeasurement() 的实际选档逻辑。
 * DDS 替身推进模拟的 GetTick() 计数，模拟 SPI 传输碰到截止时刻。
 * 测试保持在安全档，不初始化外设、不访问硬件寄存器。
 */
#include "BSP_PinMap.h"
#include "BSP_Board.h"
#include "BSP_Clock.h"
#include "BSP_GPIO.h"
#include "BSP_USART.h"
#include "DDS_Signal.h"
#include "ADC_Sample.h"
#include "Range_Switch.h"
#include "OLED_I2C.h"
#include "delay.h"

#include <stdio.h>
#include <stdlib.h>

const uint32_t rlc_frequency_hz[RLC_FREQUENCY_COUNT] = {100u, 1000u, 10000u, 100000u};

static uint32_t ticks_ms;

void Delay_Init(void) { ticks_ms = 0u; }
void Delay(uint32_t ms) { ticks_ms += ms; }
uint32_t GetTick(void) { return ticks_ms; }

static bool expire_during_reset;
static bool expire_during_start;

bool BSP_Clock_Init(void)
{
    return true;
}

void BSP_GPIO_Init(void) {}
void BSP_USART_Init(void) {}
void OLED_I2C_Init(void) {}

bool DDS_Init(void)
{
    return true;
}

bool BSP_ADC_Init(void)
{
    return true;
}

static bool dds_running, selected_high, normal_enabled;
static uint8_t preselected;

static void expect(bool expression, const char *description)
{
    if (!expression)
    {
        fprintf(stderr, "FAIL: %s\n", description);
        exit(EXIT_FAILURE);
    }
}

void Range_SelectLow(void) { expect(!dds_running, "stop before moving K0"); selected_high = false; }
void Range_DisconnectHigh(void)
{
    expect(!dds_running && !selected_high, "park to low before releasing high contact");
    preselected = 0u;
}
void Range_PreselectHigh(uint8_t index)
{
    expect(!dds_running && !selected_high && preselected == 0u && index > 0u && index < 5u,
           "one high contact preselected with K0 parked");
    preselected = index;
}
void Range_SelectHigh(uint8_t index)
{
    expect(!dds_running && !selected_high && preselected == index, "preselect target before K0");
    selected_high = true;
}
void Range_SetNormal(bool normal)
{
    expect(!normal || !dds_running, "stop before raising NORMAL");
    normal_enabled = normal;
}

bool DDS_Stop(uint32_t deadline_ms)
{
    if (expire_during_reset)
    {
        ++ticks_ms;
    }
    if (!BSP_BeforeDeadline(deadline_ms)) return false;
    dds_running = false;
    return true;
}

bool DDS_SetFrequency(uint32_t nominal_hz, uint32_t deadline_ms)
{
    (void)nominal_hz;
    if (expire_during_start)
    {
        ++ticks_ms;
    }
    dds_running = BSP_BeforeDeadline(deadline_ms);
    return dds_running;
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

static void check_selection(uint8_t frequency_index, uint8_t range_index, uint32_t remaining_ms,
                            RLC_Status expected, const char *description)
{
    const uint32_t deadline_ms = BSP_GetMillis() + remaining_ms;
    const RLC_Status actual = BSP_SelectMeasurement(frequency_index, range_index, false, deadline_ms);
    if (actual != expected)
    {
        fprintf(stderr, "FAIL: %s (status %d, expected %d)\n", description, (int)actual,
                (int)expected);
        exit(EXIT_FAILURE);
    }
}

int main(void)
{
    check_selection(RLC_FREQUENCY_COUNT, RLC_RANGE_SAFE_INDEX, 1u, RLC_STATUS_HARDWARE_ERROR,
                    "invalid frequency index");
    check_selection(RLC_FREQUENCY_1_KHZ, RLC_RANGE_COUNT, 1u, RLC_STATUS_HARDWARE_ERROR,
                    "invalid range index");
    check_selection(RLC_FREQUENCY_1_KHZ, RLC_RANGE_SAFE_INDEX, 0u, RLC_STATUS_TIMEOUT,
                    "deadline already expired");

    expire_during_reset = true;
    check_selection(RLC_FREQUENCY_1_KHZ, RLC_RANGE_SAFE_INDEX, 1u, RLC_STATUS_TIMEOUT,
                    "deadline expires during DDS reset");
    expire_during_reset = false;
    expire_during_start = true;
    check_selection(RLC_FREQUENCY_1_KHZ, RLC_RANGE_SAFE_INDEX, 1u, RLC_STATUS_TIMEOUT,
                    "deadline expires during DDS start");
    expire_during_start = false;
    const uint8_t limited[] = {0u, 3u, 5u, 9u, 17u};
    for (uint8_t index = 0u; index < 5u; ++index)
    {
        expect(Range_CoilMask(index, false) == limited[index], "five CAL masks");
        expect(Range_CoilMask(index, true) == (limited[index] | 32u), "five NORMAL masks");
    }
    expect(Range_CoilMask(5u, true) == 0u, "invalid code defaults off");
    check_selection(RLC_FREQUENCY_1_KHZ, 1u, 60u, RLC_STATUS_OK, "select high one");
    expect(selected_high && preselected == 1u && !normal_enabled, "limited high one");
    check_selection(RLC_FREQUENCY_1_KHZ, 4u, 33u, RLC_STATUS_TIMEOUT, "interrupt relay transition");
    expect(!selected_high && preselected == 0u && !normal_enabled && !dds_running, "timeout restores idle");
    expect(BSP_SelectMeasurement(RLC_FREQUENCY_1_KHZ, 4u, true, BSP_GetMillis()+80u) == RLC_STATUS_OK,
           "complete NORMAL after valid relay transition");
    expect(normal_enabled && dds_running && preselected == 4u, "normal high state");
    BSP_StopMeasurement();
    expect(!selected_high && preselected == 0u && !normal_enabled && !dds_running, "STOP restores safe wiring");
    expect(BSP_CaptureFrame(1u, BSP_GetMillis()+10u, NULL) == RLC_STATUS_HARDWARE_ERROR,
           "capture impossible before a stable selection");
    puts("PASS: relay masks, break-before-change, deadlines, CAL idle and stopped acquisition");
    return EXIT_SUCCESS;
}
