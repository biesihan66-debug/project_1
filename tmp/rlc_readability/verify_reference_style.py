from pathlib import Path
import ast
import json
import re
import subprocess
import sys

sys.stdout.reconfigure(encoding='utf-8')

WORK = Path(__file__).resolve().parent
ROOT = Path('C:/Users/1/Desktop/集训电赛/测量代码V1.1')
SYSTEM = ROOT / 'System'
BEFORE = WORK / 'before_reference_style'
MAPPING = json.loads((WORK / 'style_map.json').read_text(encoding='utf-8'))
filenames, symbols = MAPPING['filenames'], MAPPING['symbols']
file_pattern = re.compile('|'.join(map(re.escape, sorted(filenames, key=len, reverse=True))))
symbol_pattern = re.compile(r'\b(' + '|'.join(map(re.escape, sorted(symbols, key=len, reverse=True))) + r')\b')


def convert(text):
    text = file_pattern.sub(lambda m: filenames[m.group()], text)
    return symbol_pattern.sub(lambda m: symbols[m.group()], text)


def build(name, sources, include=SYSTEM, defines=()):
    executable = WORK / (name + '.exe')
    command = ['gcc', '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror', '-pedantic',
               '-I' + str(include), *defines, *map(str, sources), '-o', str(executable), '-lm']
    compiled = subprocess.run(command, capture_output=True)
    if compiled.returncode:
        print(compiled.stderr.decode('utf-8', errors='replace'))
        compiled.check_returncode()
    result = subprocess.run([str(executable)], capture_output=True, check=True)
    (WORK / (name + '.trace')).write_bytes(result.stdout)
    return result.stdout


trace = WORK / 'measurement_style_trace.c'
trace.write_text(convert((WORK / 'measurement_trace.c').read_text(encoding='utf-8')), encoding='utf-8')
current = build('measurement_reference_style', [trace, SYSTEM/'RLC_Measure.c', SYSTEM/'RLC_Calculate.c', SYSTEM/'RLC_Calibration.c'])
assert current == (WORK/'measurement_before.trace').read_bytes(), 'Measurement behavior changed'
print('PASS: all 13593 measurement result/setting trace records unchanged after style migration')

old_script = ast.parse((WORK / 'verify_refactor.py').read_text(encoding='utf-8'))
app_test = next(ast.literal_eval(node.value) for node in old_script.body if isinstance(node, ast.Assign)
                and any(isinstance(t, ast.Name) and t.id == 'app_test' for t in node.targets))
app_test = '#include "Flash_Calibration.h"\n' + convert(app_test)
(WORK / 'style_app_before.c').write_text(convert((BEFORE/'USER/main.c').read_text(encoding='utf-8')), encoding='utf-8')
(WORK / 'style_app_after.c').write_text((SYSTEM/'Key.c').read_text(encoding='utf-8') + '\n' +
                                        (SYSTEM/'App_RLC.c').read_text(encoding='utf-8'), encoding='utf-8')
for mode, defines in [('Full', []), ('Basic', ['-DRLC_APP_BASIC_ONLY=1']), ('Fixed1k', ['-DRLC_APP_FIXED_1KHZ=1'])]:
    traces=[]
    for version in ('before','after'):
        test = WORK / f'style_app_{mode}_{version}.c'
        test.write_text(app_test.replace('APP_SOURCE', f'style_app_{version}.c'), encoding='utf-8')
        traces.append(build(f'style_app_{mode}_{version}', [test], defines=defines))
    assert traces[0] == traces[1], f'App/Key behavior changed for {mode}'
    print(f'PASS: {mode} app and button traces unchanged after extraction')

output_test = r'''
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
'''
old_test, new_test = WORK/'output_before.c', WORK/'output_after.c'
old_test.write_text(output_test, encoding='utf-8')
new_test.write_text(convert(output_test).replace('#include "BSP_Board.h"',
                     '#include "BSP_Board.h"\n#include "OLED_I2C.h"'), encoding='utf-8')
old_output=build('output_before',[old_test, BEFORE/'System/oled_result_display.c', BEFORE/'System/measurement_debug_output.c'],include=BEFORE/'System')
new_output=build('output_after',[new_test,SYSTEM/'OLED_Display.c',SYSTEM/'Serial_Debug.c',SYSTEM/'Text_Format.c'])
assert old_output == new_output, 'OLED pixels or UART wire output changed'
print('PASS: OLED I2C bytes and UART output unchanged for R/L/C scales and all errors')
