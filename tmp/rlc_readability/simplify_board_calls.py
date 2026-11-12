from pathlib import Path
import re, shutil, xml.etree.ElementTree as ET

ROOT = Path('C:/Users/1/Desktop/集训电赛/测量代码V1.1')
WORK = Path(__file__).resolve().parent
BACKUP = WORK / 'before_direct_calls'
assert not BACKUP.exists()
for folder in ['System','USER','Tests']:
    shutil.copytree(ROOT/folder, BACKUP/folder)
for name in ['project.uvprojx','project.uvoptx']:
    shutil.copy2(ROOT/name, BACKUP/name)

def edit(path, text):
    path.write_text(text, encoding='utf-8', newline='\n')

def direct_calls(text):
    text = text.replace('port->millis(port->context)', 'BSP_GetMillis()')
    text = text.replace('port->select_setting(port->context, ', 'BSP_SelectMeasurement(')
    text = text.replace('port->capture(port->context, ', 'BSP_CaptureFrame(')
    text = text.replace('port->stop(port->context)', 'BSP_StopMeasurement()')
    text = text.replace('port->feedback_ohm', 'bsp_feedback_ohm')
    text = text.replace('port->feedback_capacitance_f', 'bsp_feedback_capacitance_f')
    text = text.replace('port->short_stimulus_min_codes', 'bsp_short_min_codes')
    text = re.sub(r'const RLC_Hardware \*port,\s*', '', text)
    text = re.sub(r'\bport,\s*', '', text)
    return text

p = ROOT/'System/RLC_Measure.c'
text = p.read_text(encoding='utf-8')
text = re.sub(r'static bool PortReady\([^}]+}\n\n', '', text)
text = text.replace('!PortReady(port) || ', '')
text = direct_calls(text)
text = text.replace('#include "RLC_Calculate.h"', '#include "RLC_Calculate.h"\n#include "BSP_Board.h"')
text = text.replace('App_RLC.c 传入板级端口和开短路校准记录', 'App_RLC.c 传入测量模式和开短路校准记录')
text = text.replace('feedback_capacitance_f[]（F）', 'bsp_feedback_capacitance_f[]（F）')
edit(p,text)

p=ROOT/'System/RLC_Measure.h'
text=direct_calls(p.read_text(encoding='utf-8')).replace('#include "RLC_Hardware.h"\n','')
text=text.replace('参数约定：port 填 BSP_GetMeasureHardware()；calibration 是开短路校准表。',
                  '参数约定：calibration 是开短路校准表；mode 选择基础或扩展模式。')
text=text.replace(' * 不需要在 main.c 中配置 ADC/DDS；本模块负责设置、采样、计算和停止激励。',
                  ' * App_Init() 成功后即可调用；本模块直接调用 BSP 设置、采样、计算和停止激励。')
edit(p,text)

p=ROOT/'System/BSP_Board.c'
text=p.read_text(encoding='utf-8')
text=re.sub(r'static uint32_t MeasurementMillis\([^}]+}\n\n','',text)
text=text.replace('static void StopMeasurement(void *context)', 'void BSP_StopMeasurement(void)')
text=text.replace('static RLC_Status SelectMeasurementSetting(void *context, ', 'RLC_Status BSP_SelectMeasurement(')
text=text.replace('static RLC_Status CaptureMeasurementFrame(void *context, ', 'RLC_Status BSP_CaptureFrame(')
text=text.replace('    (void)context;\n','')
text=text.replace('StopMeasurement(NULL)', 'BSP_StopMeasurement()')
start=text.index('static const RLC_Hardware measurement_port')
end=text.index('/* GPIO 基础操作',start)
text=text[:start]+'''const float bsp_feedback_ohm[RLC_RANGE_COUNT] = {10.0f, 100.0f, 1000.0f, 10000.0f, 100000.0f};
const float bsp_feedback_capacitance_f[RLC_RANGE_COUNT] = {0.0f, 0.0f, 0.0f, 0.0f, 0.0f};
const float bsp_short_min_codes = 2.0f;

'''+text[end:]
text=text.replace('measurement_port 内的反馈阻容表', 'bsp_feedback_ohm / bsp_feedback_capacitance_f 反馈阻容表')
edit(p,text)

p=ROOT/'System/BSP_Board.h'
text=p.read_text(encoding='utf-8').replace('#include "RLC_Hardware.h"','#include "RLC_Types.h"')
text=text.replace('/* 返回已经绑定好四项硬件操作和反馈参数的只读表，交给 RLC_Measure... 使用。 */\nconst RLC_Hardware *BSP_GetMeasureHardware(void);',
'''/* 量程参数实际定义在 BSP_Board.c；下标 0~4 对应五档，不是 GPIO 编号。
 * extern const 表示这里引用一份只读表；需要改实测参数时只改 .c 中的定义。 */
extern const float bsp_feedback_ohm[RLC_RANGE_COUNT];           /* 有效反馈电阻，Ω。 */
extern const float bsp_feedback_capacitance_f[RLC_RANGE_COUNT]; /* 并联反馈电容，F。 */
extern const float bsp_short_min_codes;                        /* 短路采集最低电压幅度，ADC 码。 */

/* frequency_index / range_index 填频率、反馈档下标，返回 OK 表示已切换并稳定。
 * deadline_ms 为允许等待到的绝对毫秒时刻；调用失败会停止激励。 */
RLC_Status BSP_SelectMeasurement(uint8_t frequency_index, uint8_t range_index, uint32_t deadline_ms);

/* 在已选设置下采集 cycles 个周期；frame 接收采样说明，仅 OK 时有效。 */
RLC_Status BSP_CaptureFrame(uint8_t cycles, uint32_t deadline_ms, RLC_Frame *frame);

/* 停止激励并回到安全反馈档。 */
void BSP_StopMeasurement(void);''')
edit(p,text)

p=ROOT/'System/App_RLC.c'
text=direct_calls(p.read_text(encoding='utf-8'))
text=text.replace('CaptureCalibrationStep(const RLC_Hardware *port)', 'CaptureCalibrationStep(void)')
text=text.replace('CaptureCalibrationStep(port)', 'CaptureCalibrationStep()')
text=text.replace('HandleButtonEvent(event, BSP_GetMeasureHardware(), now_ms)', 'HandleButtonEvent(event, now_ms)')
edit(p,text)

# The host test substitutes ordinary BSP functions instead of installing callbacks.
p=ROOT/'Tests/test_rlc_measurement.c'
text=p.read_text(encoding='utf-8').replace('#include "RLC_Calculate.h"', '#include "RLC_Calculate.h"\n#include "BSP_Board.h"')
text=text.replace('} TestBoard;', '} TestBoard;\n\nstatic TestBoard *active_board;')
text=text.replace('static uint32_t clock_ms(void *context)', 'uint32_t BSP_GetMillis(void)')
text=text.replace('((TestBoard *)context)->', 'active_board->')
text=text.replace('static RLC_Status select_setting(void *context, ', 'RLC_Status BSP_SelectMeasurement(')
text=text.replace('static RLC_Status capture(void *context, ', 'RLC_Status BSP_CaptureFrame(')
text=text.replace('TestBoard *board = context;', 'TestBoard *board = active_board;')
text=text.replace('static void stop(void *context)', 'void BSP_StopMeasurement(void)')
text=text.replace('static const float feedback_ohm', 'const float bsp_feedback_ohm')
text=text.replace('feedback_ohm[board->range_index]', 'bsp_feedback_ohm[board->range_index]')
start=text.index('static RLC_Hardware test_port(')
end=text.index('static void check_reading',start)
text=text[:start]+'''const float bsp_feedback_capacitance_f[RLC_RANGE_COUNT] = {0.0f};
const float bsp_short_min_codes = 2.0f;

'''+text[end:]
text=text.replace('RLC_Hardware port = test_port(&board);', 'active_board = &board;')
text=text.replace('&port, ', '')
edit(p,text)

p=ROOT/'Tests/test_board_deadline.c'
text=direct_calls(p.read_text(encoding='utf-8'))
text=text.replace('    const RLC_Hardware *port = BSP_GetMeasureHardware();\n','')
text=text.replace('通过 BSP_GetMeasureHardware() 调用实际选档逻辑', '直接调用 BSP_SelectMeasurement() 的实际选档逻辑')
edit(p,text)

for name in ['project.uvprojx','project.uvoptx']:
    tree=ET.parse(ROOT/name)
    for parent in tree.getroot().iter():
        for child in list(parent):
            if child.tag=='File' and (child.findtext('FileName')=='RLC_Hardware.h' or child.findtext('FilenameWithoutPath')=='RLC_Hardware.h'):
                parent.remove(child)
    if name=='project.uvoptx':
        for index,file in enumerate(tree.findall('./Group/File'),1):
            file.find('FileNumber').text=str(index)
    ET.indent(tree,space='  ')
    tree.write(ROOT/name,encoding='utf-8',xml_declaration=True)
(ROOT/'System/RLC_Hardware.h').unlink()
print('Removed callback table and context plumbing; direct BSP interfaces now match reference style.')
