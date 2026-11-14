/*
 * 板级入口。调用时钟、GPIO、串口和测量驱动，本文件负责 SysTick 计时与测量协调。
 * uptime_ms 是 SysTick 中断每毫秒更新的 32 位计数，主循环和采样读它判断超时。
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

/* 上电时激励停用，反馈档由板级初始化设为安全档。 */
static volatile uint32_t uptime_ms;
static uint8_t active_frequency_index;
static uint8_t active_range_index = RLC_RANGE_SAFE_INDEX;
static bool active_setting;

void SysTick_Handler(void)
{
    ++uptime_ms;
}

uint32_t BSP_GetMillis(void)
{
    return uptime_ms;
}

bool BSP_BeforeDeadline(uint32_t deadline_ms)
{
    uint32_t remaining = deadline_ms - BSP_GetMillis();

    return remaining != 0u && remaining < 0x80000000u;
}

bool BSP_Init(void)
{
    uptime_ms = 0u;
    if (!BSP_Clock_Init())
    {
        return false;
    }
    BSP_GPIO_Init();
    BSP_USART_Init();
    OLED_I2C_Init();
    return DDS_Init() && BSP_ADC_Init();
}

void BSP_StopMeasurement(void)
{
    if (active_setting)
    {
        if (DDS_Stop(BSP_GetMillis() + 3u))
        {
            uint32_t muted_ms = BSP_GetMillis() + 2u;

            while (BSP_BeforeDeadline(muted_ms))
            {
                /* 等待正弦输出退稳后回到安全档。 */
            }
        }
        active_setting = false;
    }
    Range_Set(RLC_RANGE_SAFE_INDEX);
    active_range_index = RLC_RANGE_SAFE_INDEX;
}

static bool WaitForSettling(uint32_t duration_ms, uint32_t deadline_ms)
{
    uint32_t start_ms = BSP_GetMillis();
    uint32_t remaining_ms = deadline_ms - start_ms;

    if (remaining_ms <= duration_ms || remaining_ms >= 0x80000000u)
    {
        return false;
    }
    while (BSP_BeforeDeadline(start_ms + duration_ms))
    {
        /* 空闲期间 SysTick 持续计时。 */
    }
    return BSP_BeforeDeadline(deadline_ms);
}

RLC_Status BSP_SelectMeasurement(uint8_t frequency_index, uint8_t range_index, uint32_t deadline_ms)
{
    uint32_t frequency;
    uint32_t signal_settle_ms;

    if (frequency_index >= RLC_FREQUENCY_COUNT || range_index >= RLC_RANGE_COUNT)
    {
        return RLC_STATUS_HARDWARE_ERROR;
    }
    if (!BSP_BeforeDeadline(deadline_ms))
    {
        return RLC_STATUS_TIMEOUT;
    }
    if (active_setting && active_frequency_index == frequency_index &&
        active_range_index == range_index)
    {
        return RLC_STATUS_OK;
    }

    frequency = rlc_frequency_hz[frequency_index];
    signal_settle_ms = (1000u + frequency - 1u) / frequency + 2u;
    if (!DDS_Stop(deadline_ms))
    {
        /* DDS 写入只会因截止时间到达而失败，应让上层走超时处理。 */
        BSP_StopMeasurement();
        return RLC_STATUS_TIMEOUT;
    }
    if (active_range_index != range_index)
    {
        /* 先关闭正弦激励，等待 DDS 输出退稳，再切档并等待触点稳定。 */
        if (!WaitForSettling(2u, deadline_ms))
        {
            BSP_StopMeasurement();
            return RLC_STATUS_TIMEOUT;
        }
        Range_Set(range_index);
        active_range_index = range_index;
        if (!WaitForSettling(RLC_RANGE_SETTLE_MS, deadline_ms))
        {
            BSP_StopMeasurement();
            return RLC_STATUS_TIMEOUT;
        }
    }
    if (!DDS_SetFrequency(frequency, deadline_ms))
    {
        (void)DDS_Stop(BSP_GetMillis() + 3u);
        active_setting = false;
        BSP_StopMeasurement();
        return RLC_STATUS_TIMEOUT;
    }
    active_frequency_index = frequency_index;
    active_setting = true;
    if (!WaitForSettling(signal_settle_ms, deadline_ms))
    {
        BSP_StopMeasurement();
        return RLC_STATUS_TIMEOUT;
    }
    return RLC_STATUS_OK;
}

RLC_Status BSP_CaptureFrame(uint8_t cycles, uint32_t deadline_ms, RLC_Frame *frame)
{
    if (!active_setting)
    {
        return RLC_STATUS_HARDWARE_ERROR;
    }
    return ADC_Capture(rlc_frequency_hz[active_frequency_index], DDS_GetActualFrequencyHz(), cycles,
                       deadline_ms, frame);
}

/* 示例跨阻值。补偿电容当前填写 0 F，实际装配后按频响与阻值测量结果替换。 */
const float bsp_feedback_ohm[RLC_RANGE_COUNT] = {10.0f, 100.0f, 1000.0f, 10000.0f, 100000.0f};
const float bsp_feedback_capacitance_f[RLC_RANGE_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
const float bsp_short_min_codes = 2.0f;
