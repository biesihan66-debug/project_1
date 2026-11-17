/*
 * 板级入口。调用时钟、GPIO、串口和测量驱动，本文件负责测量协调。
 * BSP_GetMillis() 读取 delay.c 的 GetTick()，单位 ms，供主循环和采样判断超时。
 * bsp_feedback_ohm / bsp_feedback_capacitance_f 反馈阻容表按档位索引，单位为 Ω 和 F，须按实物更新。
 * active_frequency_index、active_range_index 由换档函数更新，供采集使用。
 * 频率进入 OLED 第 3 行，量程或故障进入第 4 行。UART 数据不直接进入 OLED。
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

/* 上电时激励停用，反馈档由板级初始化设为安全档。 */
static uint8_t active_frequency_index;
static uint8_t active_range_index = RLC_RANGE_SAFE_INDEX;
static bool active_setting;
static bool active_normal;
static bool relays_moving;

uint32_t BSP_GetMillis(void)
{
    return GetTick();
}

bool BSP_BeforeDeadline(uint32_t deadline_ms)
{
    uint32_t remaining = deadline_ms - BSP_GetMillis();

    return remaining != 0u && remaining < 0x80000000u;
}

bool BSP_Init(void)
{
    if (!BSP_Clock_Init())
    {
        return false;
    }
    Delay_Init();
    BSP_GPIO_Init();
    BSP_USART_Init();
    OLED_I2C_Init();
    return DDS_Init() && BSP_ADC_Init();
}

void BSP_StopMeasurement(void)
{
    const bool stopped = !active_setting || DDS_Stop(BSP_GetMillis() + 3u);
    if (active_setting && stopped)
    {
        Delay(RLC_DDS_OFF_SETTLE_MS);
    }
    active_setting = false;
    /* Restore passive series limiting even when STOP has failed. */
    Range_SetNormal(false);
    if (active_normal || !stopped || relays_moving)
    {
        Delay(RLC_RANGE_SETTLE_MS);
    }
    active_normal = false;
    Range_SelectLow();
    if (active_range_index != RLC_RANGE_SAFE_INDEX || relays_moving)
    {
        Delay(RLC_RANGE_SETTLE_MS);
    }
    Range_DisconnectHigh();
    if (active_range_index != RLC_RANGE_SAFE_INDEX || relays_moving)
    {
        Delay(RLC_RANGE_SETTLE_MS);
    }
    active_range_index = RLC_RANGE_SAFE_INDEX;
    relays_moving = false;
}

static bool WaitForSettling(uint32_t duration_ms, uint32_t deadline_ms)
{
    const uint32_t remaining_ms = deadline_ms - BSP_GetMillis();
    if (remaining_ms <= duration_ms || remaining_ms >= 0x80000000u)
    {
        return false;
    }
    Delay(duration_ms);
    return BSP_BeforeDeadline(deadline_ms);
}

static bool SwitchRelays(uint8_t range_index, bool normal, uint32_t deadline_ms)
{
    relays_moving = true;
    if (active_normal && (active_range_index != range_index || !normal))
    {
        Range_SetNormal(false);
        active_normal = false;
        if (!WaitForSettling(RLC_RANGE_SETTLE_MS, deadline_ms))
        {
            return false;
        }
    }
    if (active_range_index != range_index)
    {
        /* Park K0 on NC before moving any high-range relay. */
        if (active_range_index != RLC_RANGE_SAFE_INDEX)
        {
            Range_SelectLow();
            if (!WaitForSettling(RLC_RANGE_SETTLE_MS, deadline_ms))
            {
                return false;
            }
            Range_DisconnectHigh();
            if (!WaitForSettling(RLC_RANGE_SETTLE_MS, deadline_ms))
            {
                return false;
            }
        }
        if (range_index != RLC_RANGE_SAFE_INDEX)
        {
            Range_PreselectHigh(range_index);
            if (!WaitForSettling(RLC_RANGE_SETTLE_MS, deadline_ms))
            {
                return false;
            }
            Range_SelectHigh(range_index);
            if (!WaitForSettling(RLC_RANGE_SETTLE_MS, deadline_ms))
            {
                return false;
            }
        }
        active_range_index = range_index;
    }
    if (active_normal != normal)
    {
        Range_SetNormal(normal);
        active_normal = normal;
        if (!WaitForSettling(RLC_RANGE_SETTLE_MS, deadline_ms))
        {
            return false;
        }
    }
    relays_moving = false;
    return true;
}

RLC_Status BSP_SelectMeasurement(uint8_t frequency_index, uint8_t range_index,
                                 bool normal, uint32_t deadline_ms)
{
    if (frequency_index >= RLC_FREQUENCY_COUNT || range_index >= RLC_RANGE_COUNT)
    {
        return RLC_STATUS_HARDWARE_ERROR;
    }
    if (!BSP_BeforeDeadline(deadline_ms))
    {
        return RLC_STATUS_TIMEOUT;
    }
    if (active_setting && active_frequency_index == frequency_index &&
        active_range_index == range_index && active_normal == normal)
    {
        return RLC_STATUS_OK;
    }

    const uint32_t frequency = rlc_frequency_hz[frequency_index];
    const uint32_t signal_settle_ms = (1000u + frequency - 1u) / frequency + 2u;
    if (!DDS_Stop(deadline_ms) || !WaitForSettling(RLC_DDS_OFF_SETTLE_MS, deadline_ms))
    {
        BSP_StopMeasurement();
        return RLC_STATUS_TIMEOUT;
    }
    active_setting = false;
    if (!SwitchRelays(range_index, normal, deadline_ms))
    {
        BSP_StopMeasurement();
        return RLC_STATUS_TIMEOUT;
    }
    active_setting = true; /* A partially failed start can still need STOP. */
    if (!DDS_SetFrequency(frequency, deadline_ms))
    {
        BSP_StopMeasurement();
        return RLC_STATUS_TIMEOUT;
    }
    active_frequency_index = frequency_index;
    if (!WaitForSettling(signal_settle_ms, deadline_ms))
    {
        BSP_StopMeasurement();
        return RLC_STATUS_TIMEOUT;
    }
    return RLC_STATUS_OK;
}

RLC_Status BSP_CaptureFrame(uint8_t cycles, uint32_t deadline_ms, RLC_Frame *frame)
{
    if (!active_setting || relays_moving)
    {
        return RLC_STATUS_HARDWARE_ERROR;
    }
    return ADC_Capture(rlc_frequency_hz[active_frequency_index], DDS_GetActualFrequencyHz(), cycles,
                       deadline_ms, frame);
}

/* Nominal branch in parallel with 10 MΩ. The commissioning table must be
 * filled with measured per-frequency complex terms before scoring. */
const float bsp_feedback_ohm[RLC_RANGE_COUNT] = {10.0f, 99.999f, 999.900f, 9990.010f, 99009.90f};
const float bsp_feedback_capacitance_f[RLC_RANGE_COUNT] = {
    2.2e-12f, 2.2e-12f, 24.2e-12f, 2.2e-12f, 2.2e-12f
};
