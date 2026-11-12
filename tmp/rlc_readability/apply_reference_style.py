from pathlib import Path
import json
import re
import shutil
import xml.etree.ElementTree as ET

ROOT = Path('C:/Users/1/Desktop/集训电赛/测量代码V1.1').resolve()
WORK = Path(__file__).resolve().parent
BACKUP = WORK / 'before_reference_style'
assert not BACKUP.exists(), 'Run this migration only once.'
for folder in ['System', 'USER', 'Tests', 'startup']:
    shutil.copytree(ROOT / folder, BACKUP / folder)
for name in ['project.uvprojx', 'project.uvoptx', 'build_firmware.ps1']:
    shutil.copy2(ROOT / name, BACKUP / name)

stems = {
    'automatic_rlc_measurement': 'RLC_Measure',
    'rlc_impedance_calculation': 'RLC_Calculate',
    'open_short_calibration': 'RLC_Calibration',
    'rlc_measurement_types': 'RLC_Types',
    'rlc_measurement_port': 'RLC_Hardware',
    'stm32_rlc_board': 'BSP_Board',
    'board_pin_map': 'BSP_PinMap',
    'rlc_board_internal': 'BSP_Internal',
    'stm32f103_registers': 'BSP_Registers',
    'excitation_signal': 'DDS_Signal',
    'voltage_current_sampling': 'ADC_Sample',
    'feedback_range_switch': 'Range_Switch',
    'oled_result_display': 'OLED_Display',
    'oled_i2c_transport': 'OLED_I2C',
    'calibration_flash_storage': 'Flash_Calibration',
    'measurement_debug_output': 'Serial_Debug',
    'measurement_text_format': 'Text_Format',
}
symbols = {
    'RlcComplex': 'RLC_Complex', 'RlcSampleFrame': 'RLC_Frame',
    'RlcPhasorPair': 'RLC_Phasors', 'RlcComponent': 'RLC_Component',
    'RlcStatus': 'RLC_Status', 'RlcMeasurementResult': 'RLC_Result',
    'RlcMeasurementMode': 'RLC_Mode', 'RlcCalibration': 'RLC_Calibration',
    'RlcMeasurementPort': 'RLC_Hardware', 'RlcTrial': 'MeasureTrial',
    'RlcText': 'TextBuffer',
    'rlc_measure_auto': 'RLC_MeasureAuto', 'rlc_measure_fixed': 'RLC_MeasureFixed',
    'rlc_capture_calibration': 'RLC_CaptureCalibration',
    'rlc_demodulate_frame': 'RLC_Demodulate', 'rlc_calculate_impedance': 'RLC_CalculateImpedance',
    'rlc_classify_impedance': 'RLC_Classify', 'rlc_component_value': 'RLC_GetComponentValue',
    'rlc_complex_is_finite': 'RLC_ComplexIsFinite', 'rlc_complex_divide': 'RLC_ComplexDivide',
    'rlc_calibration_set': 'Calibration_Set',
    'rlc_calibration_set_ideal_open': 'Calibration_SetIdealOpen',
    'rlc_calibration_has_pair': 'Calibration_HasPair', 'rlc_calibration_apply': 'Calibration_Apply',
    'rlc_board_initialize': 'BSP_Init', 'rlc_board_measurement_port': 'BSP_GetMeasureHardware',
    'rlc_board_millis': 'BSP_GetMillis', 'rlc_board_button_pressed': 'BSP_KeyIsPressed',
    'rlc_board_debug_write': 'BSP_DebugWrite', 'rlc_board_before_deadline': 'BSP_BeforeDeadline',
    'rlc_board_excitation_initialize': 'DDS_Init', 'rlc_board_excitation_reset': 'DDS_Stop',
    'rlc_board_excitation_start': 'DDS_SetFrequency',
    'rlc_board_excitation_actual_hz': 'DDS_GetActualFrequencyHz',
    'rlc_board_sampling_initialize': 'ADC_Init', 'rlc_board_sample_frame': 'ADC_Capture',
    'rlc_board_range_initialize': 'Range_Init', 'rlc_board_range_set': 'Range_Set',
    'rlc_board_oled_bus_initialize': 'OLED_I2C_Init', 'rlc_board_oled_write': 'OLED_I2C_Write',
    'rlc_board_load_calibration': 'Flash_LoadCalibration', 'rlc_board_save_calibration': 'Flash_SaveCalibration',
    'oled_result_display_initialize': 'OLED_Init',
    'oled_result_display_show_activity': 'OLED_ShowActivity',
    'oled_result_display_show_lines': 'OLED_ShowLines',
    'oled_result_display_show_measurement': 'OLED_ShowResult',
    'oled_result_display_status_text': 'OLED_GetStatusText',
    'measurement_debug_output_result': 'Serial_PrintResult',
    'measurement_debug_output_calibration': 'Serial_PrintCalibration',
    'stm32_gpio_configure': 'BSP_GPIO_Config', 'stm32_gpio_set': 'BSP_GPIO_Write',
    'stm32_gpio_is_high': 'BSP_GPIO_Read',
    'rlc_text_begin': 'Text_Begin', 'rlc_text_char': 'Text_AddChar',
    'rlc_text_append': 'Text_Append', 'rlc_text_unsigned': 'Text_AddUnsigned',
    'rlc_text_decimal': 'Text_AddDecimal', 'rlc_text_scientific': 'Text_AddScientific',
    'value_si': 'component_value', 're': 'real', 'im': 'imag',
    'ButtonEvent': 'KeyEvent', 'BUTTON_NO_EVENT': 'KEY_NONE',
    'BUTTON_SHORT_PRESS': 'KEY_SHORT_PRESS', 'BUTTON_LONG_PRESS': 'KEY_LONG_PRESS',
    'BUTTON_DEBOUNCE_MS': 'KEY_DEBOUNCE_MS', 'BUTTON_LONG_PRESS_MS': 'KEY_LONG_PRESS_MS',
    'read_button_event': 'Key_GetEvent',
}

# Static helpers follow the same VerbNoun spelling without becoming public APIs.
for path in list((BACKUP / 'System').glob('*.[ch]')) + [BACKUP / 'USER/main.c']:
    text = path.read_text(encoding='utf-8-sig')
    for name in re.findall(r'^static\s+(?:inline\s+)?[\w *]+?\s+(\w+)\(', text, re.M):
        symbols.setdefault(name, ''.join(word.upper() if word in ('adc', 'dds', 'i2c', 'crc') else word.capitalize()
                                        for word in name.split('_')))
for old, new in stems.items():
    symbols[old.upper() + '_H'] = new.upper() + '_H'

filename_map = {old + ext: new + ext for old, new in stems.items() for ext in ['.c', '.h']}
name_pattern = re.compile('|'.join(map(re.escape, sorted(filename_map, key=len, reverse=True))))
symbol_pattern = re.compile(r'\b(' + '|'.join(map(re.escape, sorted(symbols, key=len, reverse=True))) + r')\b')


def convert(text):
    return symbol_pattern.sub(lambda m: symbols[m.group()], name_pattern.sub(lambda m: filename_map[m.group()], text))


def write(path, text):
    assert path.resolve().is_relative_to(ROOT), path
    path.write_text(text, encoding='utf-8', newline='\n')


for folder in ['System', 'USER', 'Tests']:
    for original in (BACKUP / folder).iterdir():
        if original.suffix not in ('.c', '.h', '.ps1'):
            continue
        destination = ROOT / folder / filename_map.get(original.name, original.name)
        write(destination, convert(original.read_text(encoding='utf-8-sig')))
        old_path = ROOT / folder / original.name
        if destination != old_path:
            assert old_path.resolve().parent == (ROOT / folder).resolve()
            old_path.unlink()

# Split button state from application workflow; preserve the existing function bodies.
main = (ROOT / 'USER/main.c').read_text(encoding='utf-8')
key_constants = main[main.index('enum {'):main.index('typedef enum {')]
key_enum = main[main.index('typedef enum {'):main.index('typedef struct {')]
key_state = main[main.index('typedef struct {'):main.index('typedef enum {', main.index('typedef struct {'))]
key_function = main[main.index('/* 短按在去抖'):main.index('/* 总线事务失败')]
key_function = key_function.replace('static KeyEvent Key_GetEvent', 'KeyEvent Key_GetEvent')
write(ROOT / 'System/Key.h', '''/*
 * 按键接口：读取 PB10，识别短按与长按。硬件引脚在 BSP_PinMap.h。
 * Key_Init() 只清除去抖状态；GPIO 上拉由 BSP_Init() 配置。
 * Key_GetEvent(now_ms) 接收当前毫秒数，无等待延时；每轮主循环调用一次。
 * 返回 KEY_NONE 表示没有新动作；短按在松开后产生，长按在按住 900 ms 时产生一次。
 * 本模块不显示文字。App_RLC.c 收到事件后切换 OLED 页面或启动测量。
 */
#ifndef KEY_H
#define KEY_H

#include <stdint.h>

''' + key_enum + '''
void Key_Init(void);
KeyEvent Key_GetEvent(uint32_t now_ms);

#endif
''')
write(ROOT / 'System/Key.c', '''/*
 * 按键去抖与长短按识别。主循环调用 Key_GetEvent()，本模块只处理按键。
 * button 是本文件的静态状态；raw_changed_ms/pressed_ms 单位 ms，由 SysTick 提供。
 * raw_pressed 是原始电平，stable_pressed 是去抖后的状态；long_reported 防止重复触发。
 * 不直接写 OLED；短按/长按事件交给 App_RLC.c，最终由应用决定 1~4 行内容。
 */
#include "Key.h"
#include "BSP_Board.h"

''' + key_constants + key_state + '''static ButtonState button;

void Key_Init(void)
{
    button = (ButtonState){0};
}

''' + key_function)

app = main[:main.index('enum {')] + main[main.index('typedef enum {', main.index('typedef struct {')):main.index('/* 短按在去抖')]
app = app.replace('static ButtonState button;\n', '')
app += main[main.index('/* 总线事务失败'):main.index('int main(void)')]
app = app.replace('#include "RLC_Measure.h"', '#include "App_RLC.h"\n#include "Key.h"\n#include "RLC_Measure.h"')
app = app.replace(' * 电桥主程序。PB10 为低有效按键，电平通过 BSP_Board.h 读取。',
                  ' * 电桥应用流程。入口 App_Init()/App_Run() 由 USER/main.c 调用。')
app = app.replace(' * 主循环只做两件事：读取已去抖的按键事件，然后处理测量或校准。',
                  ' * App_Run() 只做两件事：读取按键事件，然后处理测量或校准。')
app = app.replace(' * button 记录按键电平、去抖时间和长按是否已上报，时间来自 SysTick，单位 ms。',
                  ' * button 的按键状态已封装在 Key.c，应用只读取 KeyEvent。')
app = app.replace(' * 变量均在本文件内：', ' * 应用状态与位置：')
app += '''/* 上电初始化：硬件 -> 按键 -> 加载校准 -> 显示待机页面。 */
bool App_Init(void)
{
    if (!BSP_Init())
    {
        BSP_DebugWrite("BOARD INIT ERROR\\r\\n");
        return false;
    }
    Key_Init();
    if (Flash_LoadCalibration(&pending_calibration, sizeof pending_calibration))
    {
        active_calibration = pending_calibration;
    }
    EnsureDisplay();
    ShowReady(RLC_APP_FIXED_1KHZ ? "BENCH TEST" : "HOLD FOR CAL");
    BSP_DebugWrite("RLC READY\\r\\n");
    return true;
}

/* 每轮调用一次；无按键事件时立即返回，测量时由各模块管理超时。 */
void App_Run(void)
{
    const uint32_t now_ms = BSP_GetMillis();
    const KeyEvent event = Key_GetEvent(now_ms);
    if (event != KEY_NONE)
    {
        HandleButtonEvent(event, BSP_GetMeasureHardware(), now_ms);
    }
}
'''
write(ROOT / 'System/App_RLC.c', app)
write(ROOT / 'System/App_RLC.h', '''/*
 * 电桥应用入口。USER/main.c 只需要包含这一个头文件。
 * 阅读顺序：main.c -> App_RLC.c（按键和校准流程）-> RLC_Measure.h（测量接口）。
 * 测量结果在 RLC_Types.h 的 RLC_Result 中；OLED 第 1~4 行为类型、数值、频率、反馈档/错误。
 * 本头文件不定义共享变量，应用状态保存在 App_RLC.c 内部。
 */
#ifndef APP_RLC_H
#define APP_RLC_H

#include <stdbool.h>

/* 初始化硬件、按键和校准记录，显示待机页。成功为 true；失败时不要继续测量。 */
bool App_Init(void);

/* 在主循环内反复调用：读取按键，执行一次测量或当前校准步骤，无需传参数。 */
void App_Run(void);

#endif
''')
write(ROOT / 'USER/main.c', '''/*
 * RLC 电桥 V1.1 程序入口：先初始化，再反复处理按键和测量。
 * 功能实现均封装在 System；具体流程从 App_RLC.c 开始阅读。
 * 本文件没有测量变量。结果保存在 RLC_Result，显示位置见 OLED_Display.h。
 */
#include "App_RLC.h"

int main(void)
{
    if (!App_Init())
    {
        while (1)
        {
            /* 板级初始化失败，串口已输出原因，等待复位后重新初始化。 */
        }
    }

    while (1)
    {
        App_Run();
    }
}
''')

# Headers contain declarations; move shared text and GPIO implementations into C files.
text_header = (ROOT / 'System/Text_Format.h').read_text(encoding='utf-8')
text_start = text_header.index('/* capacity')
text_functions = text_header[text_start:text_header.rindex('#endif')].replace('static inline ', '')
text_declarations = '\n'.join(m.group(0).replace('static inline ', '').replace('\n', ' ') + ';'
                               for m in re.finditer(r'^static inline [\w *]+\([^)]*\)', text_header, re.M))
write(ROOT / 'System/Text_Format.c', text_header[:text_header.index('#ifndef')] +
      '#include "Text_Format.h"\n#include <float.h>\n\n' + text_functions)
write(ROOT / 'System/Text_Format.h', text_header[:text_start].replace('#include <float.h>\n', '') +
      '/* 缓冲区由调用者提供；各追加函数保证保留字符串结束符。实现位于 Text_Format.c。 */\n' +
      text_declarations + '\n\n#endif\n')
registers = (ROOT / 'System/BSP_Registers.h').read_text(encoding='utf-8')
gpio_start = registers.index('static inline ')
gpio_functions = registers[gpio_start:registers.rindex('#endif')].replace('static inline ', '')
gpio_declarations = '\n'.join(m.group(0).replace('static inline ', '').replace('\n', ' ') + ';'
                              for m in re.finditer(r'^static inline [\w *]+\([^)]*\)', registers, re.M))
write(ROOT / 'System/BSP_Registers.h', registers[:gpio_start] + gpio_declarations + '\n\n#endif\n')
board = ROOT / 'System/BSP_Board.c'
write(board, board.read_text(encoding='utf-8') + '\n/* GPIO 基础操作，声明位于 BSP_Registers.h。 */\n' + gpio_functions)

# Keep target options untouched; regroup the same sources and the extracted modules.
tree = ET.parse(BACKUP / 'project.uvprojx')
groups = {
    'USER - Entry': ['USER/main.c'],
    'System - Application': ['System/'+name for name in ['App_RLC.c','App_RLC.h','Key.c','Key.h','OLED_Display.c','OLED_Display.h','Serial_Debug.c','Serial_Debug.h','Text_Format.c','Text_Format.h']],
    'System - Measurement': ['System/'+name for name in ['RLC_Measure.c','RLC_Measure.h','RLC_Calculate.c','RLC_Calculate.h','RLC_Calibration.c','RLC_Calibration.h','RLC_Types.h','RLC_Hardware.h']],
    'System - Hardware': ['System/'+name for name in ['BSP_Board.c','BSP_Board.h','BSP_PinMap.h','BSP_Internal.h','BSP_Registers.h','DDS_Signal.c','ADC_Sample.c','Range_Switch.c','OLED_I2C.c','Flash_Calibration.c']],
    'Startup': ['startup/startup_stm32f103c8.s', 'startup/stm32f103c8_flash.sct'],
}
for target in tree.findall('./Targets/Target'):
    old_groups = target.find('Groups')
    old_groups.clear()
    for name, paths in groups.items():
        group = ET.SubElement(old_groups, 'Group')
        ET.SubElement(group, 'GroupName').text = name
        files = ET.SubElement(group, 'Files')
        for path in paths:
            assert (ROOT / path).is_file(), path
            file = ET.SubElement(files, 'File')
            ET.SubElement(file, 'FileName').text = Path(path).name
            ET.SubElement(file, 'FileType').text = '1' if path.endswith('.c') else ('2' if path.endswith('.s') else '5')
            ET.SubElement(file, 'FilePath').text = '.\\' + path.replace('/', '\\')
ET.indent(tree, space='  ')
tree.write(ROOT / 'project.uvprojx', encoding='utf-8', xml_declaration=True)
write(ROOT / 'project.uvoptx', convert((BACKUP / 'project.uvoptx').read_text(encoding='utf-8-sig')))
(WORK / 'style_map.json').write_text(json.dumps({'filenames': filename_map, 'symbols': symbols}, ensure_ascii=False, indent=2), encoding='utf-8')
print('Renamed modules, extracted Key/App/Text implementations, and updated all 3 Keil targets.')
