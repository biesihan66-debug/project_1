
#include "oled_result_display.h"
#include "measurement_debug_output.h"
#include "stm32_rlc_board.h"
#include <math.h>
#include <stdio.h>
bool rlc_board_oled_write(uint8_t control, const uint8_t *data, uint16_t count)
{
    printf("I2C %02x %u ", control, count);
    for (uint16_t i=0; i<count; ++i) printf("%02x", data[i]);
    puts(""); return true;
}
void rlc_board_debug_write(const char *text) { printf("UART %s", text); }
int main(void)
{
    RlcMeasurementResult result = {.status=RLC_STATUS_OK, .component=RLC_COMPONENT_R,
        .value_si=1000.0f, .frequency_hz=1000, .range_index=2, .calibrated=true,
        .elapsed_ms=37, .impedance_ohm={1000.0f,-0.125f},
        .phasors={{0.7071f,0.0f},{-0.7071f,0.001f},877.0f,877.5f}};
    if (!oled_result_display_initialize()) return 1;
    oled_result_display_show_activity("MEASURING");
    oled_result_display_show_lines("CAL OPEN", "REMOVE COMPONENT", "PRESS WHEN READY", "HOLD TO CANCEL");
    for (int kind=RLC_COMPONENT_R; kind<=RLC_COMPONENT_C; ++kind)
    {
        result.component=(RlcComponent)kind;
        for (int step=-12; step<=6; ++step)
        {
            result.value_si=powf(10.0f,(float)step);
            result.calibrated=(step%2==0);
            oled_result_display_show_measurement(&result,1000.0f);
            measurement_debug_output_result(&result,43,true);
        }
    }
    for (int status=RLC_STATUS_TIMEOUT; status<=RLC_STATUS_CALIBRATION_ERROR; ++status)
    {
        result.status=(RlcStatus)status;
        oled_result_display_show_measurement(&result,1000.0f);
        measurement_debug_output_result(&result,43,false);
        measurement_debug_output_calibration(true,(RlcStatus)status);
        measurement_debug_output_calibration(false,(RlcStatus)status);
    }
    return 0;
}
