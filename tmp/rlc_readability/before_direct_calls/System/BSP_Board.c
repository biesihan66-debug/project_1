/*
 * STM32F103 板级入口。负责时钟、SysTick、按键、串口及测量接口。
 * uptime_ms 是 SysTick 中断每毫秒更新的 32 位计数，主循环和采样读它判断超时。
 * measurement_port 内的反馈阻容表按档位索引，单位为 Ω 和 F，须按实物更新。
 * active_frequency_index、active_range_index 由换档函数更新，供采集使用。
 * 频率进入 OLED 第 3 行，量程或故障进入第 4 行。UART 数据不直接进入 OLED。
 */
#include "BSP_PinMap.h"
#include "BSP_Board.h"
#include "DDS_Signal.h"
#include "ADC_Sample.h"
#include "Range_Switch.h"
#include "OLED_I2C.h"

#define RCC_CR_HSION        (1u << 0)
#define RCC_CR_HSIRDY       (1u << 1)
#define RCC_CR_HSEON        (1u << 16)
#define RCC_CR_HSERDY       (1u << 17)
#define RCC_CR_PLLON        (1u << 24)
#define RCC_CR_PLLRDY       (1u << 25)
#define RCC_CFGR_SWS        (3u << 2)
#define RCC_CFGR_PLL_ACTIVE (2u << 2)
#define RCC_CLOCK_ATTEMPTS  500000u

#define USART_SR  STM32_REG32(STM32_USART1 + 0x00u)
#define USART_DR  STM32_REG32(STM32_USART1 + 0x04u)
#define USART_BRR STM32_REG32(STM32_USART1 + 0x08u)
#define USART_CR1 STM32_REG32(STM32_USART1 + 0x0Cu)

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

static bool WaitForClock(volatile uint32_t *reg, uint32_t mask, uint32_t desired)
{
    uint32_t attempt;

    for (attempt = 0u; attempt < RCC_CLOCK_ATTEMPTS; ++attempt)
    {
        if ((*reg & mask) == desired)
        {
            return true;
        }
    }
    return false;
}

static bool InitializeClocks(void)
{
    STM32_RCC_CR |= RCC_CR_HSION;
    if (!WaitForClock(&STM32_RCC_CR, RCC_CR_HSIRDY, RCC_CR_HSIRDY))
    {
        return false;
    }
    STM32_RCC_CFGR &= ~3u;
    if (!WaitForClock(&STM32_RCC_CFGR, RCC_CFGR_SWS, 0u))
    {
        return false;
    }
    STM32_RCC_CR &= ~RCC_CR_PLLON;
    if (!WaitForClock(&STM32_RCC_CR, RCC_CR_PLLRDY, 0u))
    {
        return false;
    }
    STM32_RCC_CR |= RCC_CR_HSEON;
    if (!WaitForClock(&STM32_RCC_CR, RCC_CR_HSERDY, RCC_CR_HSERDY))
    {
        return false;
    }
    STM32_REG32(STM32_FLASH) = (1u << 4) | 2u; /* 预取和 2 个等待周期。 */
    /* APB1 除 2，APB2 不分频，ADC 除 6，PLL 输入 HSE 并乘 9。 */
    STM32_RCC_CFGR = (4u << 8) | (2u << 14) | (1u << 16) | (7u << 18);
    STM32_RCC_CR |= RCC_CR_PLLON;
    if (!WaitForClock(&STM32_RCC_CR, RCC_CR_PLLRDY, RCC_CR_PLLRDY))
    {
        return false;
    }
    STM32_RCC_CFGR |= 2u;
    return WaitForClock(&STM32_RCC_CFGR, RCC_CFGR_SWS, RCC_CFGR_PLL_ACTIVE);
}

static void InitializeTick(void)
{
    uptime_ms = 0u;
    STM32_REG32(STM32_SYSTICK + 0x00u) = 0u;
    STM32_REG32(STM32_SYSTICK + 0x04u) = (RLC_SYSTEM_CLOCK_HZ / 1000u) - 1u;
    STM32_REG32(STM32_SYSTICK + 0x08u) = 0u;
    STM32_REG32(STM32_SYSTICK + 0x00u) = 7u;
}

static void InitializeButtonAndUart(void)
{
    BSP_GPIO_Write(RLC_BUTTON_GPIO, RLC_BUTTON_PIN, true);
    BSP_GPIO_Config(RLC_BUTTON_GPIO, RLC_BUTTON_PIN, STM32_GPIO_PULL_IN);

    STM32_RCC_APB2ENR |= 1u << 14;
    BSP_GPIO_Config(RLC_DEBUG_GPIO, RLC_DEBUG_TX_PIN, STM32_GPIO_AF_PP);
    BSP_GPIO_Config(RLC_DEBUG_GPIO, RLC_DEBUG_RX_PIN, STM32_GPIO_FLOAT_IN);
    USART_BRR = (RLC_SYSTEM_CLOCK_HZ + RLC_DEBUG_BAUD / 2u) / RLC_DEBUG_BAUD;
    USART_CR1 = (1u << 13) | (1u << 3); /* 8-N-1，只启用发送。 */
}

bool BSP_Init(void)
{
    if (!InitializeClocks())
    {
        return false;
    }
    InitializeTick();
    STM32_RCC_APB2ENR |= (1u << 0) | (1u << 2) | (1u << 3);
    Range_Init();
    InitializeButtonAndUart();
    OLED_I2C_Init();
    return DDS_Init() && ADC_Init();
}

bool BSP_KeyIsPressed(void)
{
    return !BSP_GPIO_Read(RLC_BUTTON_GPIO, RLC_BUTTON_PIN);
}

void BSP_DebugWrite(const char *text)
{
    uint32_t deadline_ms;

    if (text == NULL || (USART_CR1 & (1u << 13)) == 0u)
    {
        return;
    }
    deadline_ms = BSP_GetMillis() + 100u;
    while (*text != '\0')
    {
        while ((USART_SR & (1u << 7)) == 0u)
        {
            if (!BSP_BeforeDeadline(deadline_ms))
            {
                return;
            }
        }
        USART_DR = (uint8_t)*text++;
    }
}

static uint32_t MeasurementMillis(void *context)
{
    (void)context;
    return BSP_GetMillis();
}

static void StopMeasurement(void *context)
{
    (void)context;
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

static RLC_Status SelectMeasurementSetting(void *context, uint8_t frequency_index,
                                           uint8_t range_index, uint32_t deadline_ms)
{
    uint32_t frequency;
    uint32_t signal_settle_ms;

    (void)context;
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
        StopMeasurement(NULL);
        return RLC_STATUS_TIMEOUT;
    }
    if (active_range_index != range_index)
    {
        /* 先关闭正弦激励，等待 DDS 输出退稳，再切档并等待触点稳定。 */
        if (!WaitForSettling(2u, deadline_ms))
        {
            StopMeasurement(NULL);
            return RLC_STATUS_TIMEOUT;
        }
        Range_Set(range_index);
        active_range_index = range_index;
        if (!WaitForSettling(RLC_RANGE_SETTLE_MS, deadline_ms))
        {
            StopMeasurement(NULL);
            return RLC_STATUS_TIMEOUT;
        }
    }
    if (!DDS_SetFrequency(frequency, deadline_ms))
    {
        (void)DDS_Stop(BSP_GetMillis() + 3u);
        active_setting = false;
        StopMeasurement(NULL);
        return RLC_STATUS_TIMEOUT;
    }
    active_frequency_index = frequency_index;
    active_setting = true;
    if (!WaitForSettling(signal_settle_ms, deadline_ms))
    {
        StopMeasurement(NULL);
        return RLC_STATUS_TIMEOUT;
    }
    return RLC_STATUS_OK;
}

static RLC_Status CaptureMeasurementFrame(void *context, uint8_t cycles, uint32_t deadline_ms,
                                          RLC_Frame *frame)
{
    (void)context;
    if (!active_setting)
    {
        return RLC_STATUS_HARDWARE_ERROR;
    }
    return ADC_Capture(rlc_frequency_hz[active_frequency_index], DDS_GetActualFrequencyHz(), cycles,
                       deadline_ms, frame);
}

/* 示例跨阻值。补偿电容当前填写 0 F，实际装配后按频响与阻值测量结果替换。 */
static const RLC_Hardware measurement_port = {
    .context = NULL,
    .millis = MeasurementMillis,
    .select_setting = SelectMeasurementSetting,
    .capture = CaptureMeasurementFrame,
    .stop = StopMeasurement,
    .feedback_ohm = {10.0f, 100.0f, 1000.0f, 10000.0f, 100000.0f},
    .feedback_capacitance_f = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f},
    .short_stimulus_min_codes = 2.0f};

const RLC_Hardware *BSP_GetMeasureHardware(void)
{
    return &measurement_port;
}

/* GPIO 基础操作，声明位于 BSP_Registers.h。 */
void BSP_GPIO_Config(uint32_t gpio, uint8_t pin, uint32_t mode)
{
    uint32_t offset = pin < 8u ? 0u : 4u;
    uint32_t shift = (uint32_t)(pin % 8u) * 4u;
    volatile uint32_t *config = &STM32_REG32(gpio + offset);

    *config = (*config & ~(0xFu << shift)) | (mode << shift);
}

void BSP_GPIO_Write(uint32_t gpio, uint8_t pin, bool high)
{
    uint32_t shift = (uint32_t)pin + (high ? 0u : 16u);

    STM32_REG32(gpio + 0x10u) = 1u << shift;
}

bool BSP_GPIO_Read(uint32_t gpio, uint8_t pin)
{
    return (STM32_REG32(gpio + 0x08u) & (1u << pin)) != 0u;
}
