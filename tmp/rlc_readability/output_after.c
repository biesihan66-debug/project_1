
#include "OLED_Display.h"
#include "Serial_Debug.h"
#include "BSP_Board.h"
#include "OLED_I2C.h"
#include <math.h>
#include <stdio.h>
bool OLED_I2C_Write(uint8_t control, const uint8_t *data, uint16_t count)
{
    printf("I2C %02x %u ", control, count);
    for (uint16_t i=0; i<count; ++i) printf("%02x", data[i]);
    puts(""); return true;
}
void BSP_DebugWrite(const char *text) { printf("UART %s", text); }
int main(void)
{
    RLC_Result result = {.status=RLC_STATUS_OK, .component=RLC_COMPONENT_R,
        .component_value=1000.0f, .frequency_hz=1000, .range_index=2, .calibrated=true,
        .elapsed_ms=37, .impedance_ohm={1000.0f,-0.125f},
        .phasors={{0.7071f,0.0f},{-0.7071f,0.001f},877.0f,877.5f}};
    if (!OLED_Init()) return 1;
    OLED_ShowActivity("MEASURING");
    OLED_ShowLines("CAL OPEN", "REMOVE COMPONENT", "PRESS WHEN READY", "HOLD TO CANCEL");
    for (int kind=RLC_COMPONENT_R; kind<=RLC_COMPONENT_C; ++kind)
    {
        result.component=(RLC_Component)kind;
        for (int step=-12; step<=6; ++step)
        {
            result.component_value=powf(10.0f,(float)step);
            result.calibrated=(step%2==0);
            OLED_ShowResult(&result,1000.0f);
            Serial_PrintResult(&result,43,true);
        }
    }
    for (int status=RLC_STATUS_TIMEOUT; status<=RLC_STATUS_CALIBRATION_ERROR; ++status)
    {
        result.status=(RLC_Status)status;
        OLED_ShowResult(&result,1000.0f);
        Serial_PrintResult(&result,43,false);
        Serial_PrintCalibration(true,(RLC_Status)status);
        Serial_PrintCalibration(false,(RLC_Status)status);
    }
    return 0;
}
