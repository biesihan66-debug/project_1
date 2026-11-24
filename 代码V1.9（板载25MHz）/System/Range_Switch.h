#ifndef RANGE_SWITCH_H
#define RANGE_SWITCH_H

#include <stdbool.h>
#include <stdint.h>

/* Calls that move contacts are valid only after DDS STOP and signal decay. */
void Range_Init(void);
void Range_SelectLow(void);
void Range_SelectHigh(uint8_t index);
void Range_DisconnectHigh(void);
void Range_PreselectHigh(uint8_t index);
void Range_SetNormal(bool normal);
uint8_t Range_CoilMask(uint8_t index, bool normal);

#endif
