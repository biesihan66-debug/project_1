#ifndef RLC_COMMISSIONING_H
#define RLC_COMMISSIONING_H

/* [wiring (2W, 4W)][frequency][range], ohms. Includes both channel
 * transfer functions under the final fixture. Zero = nominal bench model.
 * Populate with real fixture/board data, never with simulated results. */
static const RLC_Complex rlc_effective_feedback[2][RLC_FREQUENCY_COUNT][RLC_RANGE_COUNT] = {
    {{{0.0f, 0.0f}}}
};

#endif
