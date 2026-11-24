/* Relay drive wiring: PB0/PB1/PB11/PB12/PB13/PB14 -> ULN2003LV IN1..6. */
#include "Range_Switch.h"
#include "BSP_PinMap.h"
#include "RLC_Types.h"

#ifndef RLC_HOST_RELAY_TEST
#include "stm32h7xx_ll_gpio.h"
#endif

uint8_t Range_CoilMask(uint8_t index, bool normal)
{
    if (index >= RLC_RANGE_COUNT)
    {
        return 0u;
    }
    const uint8_t feedback = index == 0u ? 0u : (uint8_t)(1u | (1u << index));
    return feedback | (normal ? (1u << 5) : 0u);
}

#ifndef RLC_HOST_RELAY_TEST
#define HIGH_RELAYS (RLC_RELAY_K1_PIN | RLC_RELAY_K2_PIN | RLC_RELAY_K3_PIN | RLC_RELAY_K4_PIN)
#define ALL_RELAYS (RLC_RELAY_K0_PIN | HIGH_RELAYS | RLC_RELAY_KCAL_PIN)
static const uint32_t high_pin[RLC_RANGE_COUNT] = {
    0u, RLC_RELAY_K1_PIN, RLC_RELAY_K2_PIN, RLC_RELAY_K3_PIN, RLC_RELAY_K4_PIN
};

void Range_Init(void)
{
    LL_GPIO_InitTypeDef gpio = {0};
    LL_GPIO_ResetOutputPin(RLC_RANGE_GPIO, ALL_RELAYS);
    gpio.Pin = ALL_RELAYS;
    gpio.Mode = LL_GPIO_MODE_OUTPUT;
    gpio.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio.Speed = LL_GPIO_SPEED_FREQ_LOW;
    LL_GPIO_Init(RLC_RANGE_GPIO, &gpio);
}

void Range_SelectLow(void)
{
    LL_GPIO_ResetOutputPin(RLC_RANGE_GPIO, RLC_RELAY_K0_PIN);
}

void Range_DisconnectHigh(void)
{
    LL_GPIO_ResetOutputPin(RLC_RANGE_GPIO, HIGH_RELAYS);
}

void Range_PreselectHigh(uint8_t index)
{
    if (index > 0u && index < RLC_RANGE_COUNT)
    {
        const uint32_t pin = high_pin[index];
        RLC_RANGE_GPIO->BSRR = ((HIGH_RELAYS & ~pin) << 16) | pin;
    }
}

void Range_SelectHigh(uint8_t index)
{
    if ((Range_CoilMask(index, false) & 1u) != 0u)
    {
        LL_GPIO_SetOutputPin(RLC_RANGE_GPIO, RLC_RELAY_K0_PIN);
    }
}

void Range_SetNormal(bool normal)
{
    if (normal)
    {
        LL_GPIO_SetOutputPin(RLC_RANGE_GPIO, RLC_RELAY_KCAL_PIN);
    }
    else
    {
        LL_GPIO_ResetOutputPin(RLC_RANGE_GPIO, RLC_RELAY_KCAL_PIN);
    }
}
#endif
