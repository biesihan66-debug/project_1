from pathlib import Path
import re
import shutil
import xml.etree.ElementTree as ET
from zipfile import ZipFile

parent = Path('C:/Users/1/Desktop/集训电赛')
source = parent / '测量代码V1.2'
dest = parent / '代码V1.2'
archive = parent.parent / '电赛/Codex_Repaired_3.1_STLINK.zip'
assert source.is_dir() and archive.is_file() and not dest.exists()


def read(path):
    return (dest / path).read_text(encoding='utf-8-sig')


def write(path, text):
    (dest / path).write_text(text, encoding='utf-8', newline='\r\n')


def replace(text, old, new):
    assert text.count(old) == 1, old
    return text.replace(old, new)


def edit(path, old, new):
    write(path, replace(read(path), old, new))


def region(text, start, end, new):
    first = text.index(start)
    last = text.index(end, first)
    return text[:first] + new + text[last:]


with ZipFile(archive) as z:
    names = ('OLED.c', 'OLED.h', 'OLED_Data.c', 'OLED_Data.h', 'delay.c', 'delay.h')
    reference = {n: z.read(next(p for p in z.namelist() if p.endswith('/System/' + n))) for n in names}
    config = z.read(next(p for p in z.namelist() if p.endswith('/measure_config.h')))
    assert b'#define OLED_POWERUP_DELAY_MS             100UL' in config
    assert '江协科技' in reference['OLED.c'].decode('utf-8-sig')
    assert '铁头山羊' in reference['delay.c'].decode('utf-8-sig')

dest.mkdir()
for name in ('System', 'Library', 'USER', 'startup', 'Tests', 'DebugConfig'):
    shutil.copytree(source / name, dest / name)
for name in ('project.uvprojx', 'project.uvoptx', 'build_firmware.ps1', '.clang-format'):
    shutil.copy2(source / name, dest / name)
for name, data in reference.items():
    (dest / 'System' / name).write_bytes(data)

# Reuse the reference time base. The public BSP facade stays available to the RLC core.
edit('System/delay.c', '#include "delay.h"', '''/* 本项目在系统时钟就绪后启动 SysTick。ulTicks 每毫秒递增，GetTick() 供按键及超时判断。 */
#include "stm32f10x.h"
#include "delay.h"''')
edit('System/delay.c', '__IO uint32_t ulTicks;\n', '''__IO uint32_t ulTicks;

/* 范例的 SysTick 回调在这里与计数器放在一起。 */
void SysTick_Handler(void)
{
    ulTicks++;
}
''')
edit('System/delay.h', '#include "stm32f10x.h"\n\nextern __IO uint32_t ulTicks;',
     '#include <stdint.h>\n\n/* Delay_Init() 后由 SysTick 每毫秒递增，GetTick() 返回当前值。 */\nextern volatile uint32_t ulTicks;')

board = read('System/BSP_Board.c')
board = replace(board, '本文件负责 SysTick 计时与测量协调。', '本文件负责测量协调。')
board = replace(board, 'uptime_ms 是 SysTick 中断每毫秒更新的 32 位计数，主循环和采样读它判断超时。',
                       'BSP_GetMillis() 读取 delay.c 的 GetTick()，单位 ms，供主循环和采样判断超时。')
board = replace(board, '#include "OLED_I2C.h"', '#include "OLED_I2C.h"\n#include "delay.h"')
board = replace(board, 'static volatile uint32_t uptime_ms;\n', '')
board = region(board, 'void SysTick_Handler(void)', 'uint32_t BSP_GetMillis', '')
board = replace(board, 'return uptime_ms;', 'return GetTick();')
board = replace(board, '    uptime_ms = 0u;\n', '')
board = replace(board, '    BSP_GPIO_Init();', '    Delay_Init();\n    BSP_GPIO_Init();')
board = region(board, '            uint32_t muted_ms', '        }\n        active_setting',
                      '            Delay(2u); /* 激励退稳后回安全档。 */\n')
write('System/BSP_Board.c', board)
clock = read('System/BSP_Clock.c')
clock = replace(clock, ' * BSP_Clock_Init() 启用 1 ms SysTick，BSP_Board.c 的 uptime_ms 在中断中递增。',
                      ' * BSP_Clock_Init() 更新 SystemCoreClock，随后 BSP_Init() 调用 Delay_Init()。')
clock = region(clock, '    if (SysTick_Config', '    return true;\n}', '')
write('System/BSP_Clock.c', clock)
edit('System/BSP_Clock.h', ' * F103 板级系统时钟与 1 ms SysTick。由 BSP_Init() 在外设初始化前调用。\n'
     ' * 返回 true 表示 72 MHz 时钟和计时可用；失败时应用停止初始化。\n'
     ' * SysTick_Handler() 和 uptime_ms 在 BSP_Board.c。本模块不输出到 OLED。',
     ' * F103 系统时钟，由 BSP_Init() 在外设初始化前调用。\n'
     ' * 返回 true 表示 72 MHz 时钟可用。随后由 delay.c 启用 1 ms SysTick。\n'
     ' * 时钟配置失败时应用停止初始化。本模块不输出到 OLED。')
adc = read('System/ADC_Sample.c')
adc = replace(adc, '#include "BSP_Board.h"', '#include "BSP_Board.h"\n#include "delay.h"')
adc = replace(adc, '    uint32_t ready_ms;\n', '')
adc = region(adc, '    ready_ms = BSP_GetMillis() + 2u;', '    if (!CalibrateADC',
                  '    Delay(2u); /* ADC 上电稳定后再校准。 */\n')
write('System/ADC_Sample.c', adc)

# Keep the reference OLED commands, framebuffer, glyphs and drawing APIs. Adapt
# the transport to the existing hardware I2C and its deadline/recovery handling.
oled = read('System/OLED.c')
oled = replace(oled, '#include "stm32f10x.h"\n', '')
oled = replace(oled, '#include "measure_config.h"', '#include "OLED_I2C.h"')
oled = replace(oled, 'static uint8_t OLED_TransactionFailed;\n', '')
oled = region(oled, '/*引脚配置', '/*硬件配置', '''/* RLC V1.2 适配：原软件 I2C 改用 OLED_I2C.c 的硬件 I2C1。
 * 引脚与地址由 BSP_PinMap.h 配置。一次更新遇到传输失败即停止。
 */
#define OLED_POWERUP_DELAY_MS 100u

static void OLED_WriteCommand(uint8_t Command)
{
    if (OLED_LastError == OLED_ERROR_NONE && !OLED_I2C_Write(0x00u, &Command, 1u))
    {
        OLED_LastError = OLED_ERROR_BUS;
    }
}

static void OLED_WriteData(const uint8_t *Data, uint8_t Count)
{
    if (OLED_LastError == OLED_ERROR_NONE && !OLED_I2C_Write(0x40u, Data, Count))
    {
        OLED_LastError = OLED_ERROR_BUS;
    }
}

''')
oled = region(oled, '    uint8_t init_error;', '\t/*写入一系列的命令', '''    const uint32_t now_ms = GetTick();
    if (now_ms < OLED_POWERUP_DELAY_MS)
    {
        Delay(OLED_POWERUP_DELAY_MS - now_ms);
    }
    OLED_LastError = OLED_ERROR_NONE;

''')
oled = replace(oled, '\tOLED_WriteCommand(0xCF);', '\tOLED_WriteCommand(0x8F);')
oled = replace(oled, '\tOLED_WriteCommand(0x30);', '\tOLED_WriteCommand(0x40);')
oled = region(oled, '\tOLED_WriteCommand(0xAF);', '\n}\n', '''    OLED_WriteCommand(0x20); /* SSD1306 页寻址，供 OLED_SetCursor() 使用。 */
    OLED_WriteCommand(0x02);
    OLED_WriteCommand(0x2E); /* 关闭滚动。 */
    if (OLED_LastError != OLED_ERROR_NONE)
    {
        return;
    }
    OLED_Clear();
    OLED_Update();
    OLED_WriteCommand(0xAF); /* 清完显存后开屏。 */''')
oled = replace(oled, 'char *String, uint8_t FontSize)', 'const char *String, uint8_t FontSize)')
write('System/OLED.c', oled)
header = read('System/OLED.h')
header = region(header, '/* Last initialization', '/*********************参数宏定义', '''/* 初始化或最近一次 OLED_Update / OLED_UpdateArea 的发送状态。 */
#define OLED_ERROR_NONE 0U
#define OLED_ERROR_BUS  1U
extern uint8_t OLED_DisplayBuf[8][128];
uint8_t OLED_GetLastError(void);

''')
header = replace(header, 'char *String, uint8_t FontSize)', 'const char *String, uint8_t FontSize)')
write('System/OLED.h', header)

display = read('System/OLED_Display.c')
display = '''/*
 * RLC 页面排版。显示驱动和字模复用江协科技的 OLED.c / OLED_Data.c。
 * 逻辑行 1 至 4 对应 y=0、16、32、48 像素，使用 6×8 字模。
 * value_line、frequency_line、range_line 分别是第 2、3、4 行文本。
 * result->component 决定第 1 行，result->status 的失败提示在第 4 行。
 */
#include "OLED_Display.h"
#include "OLED.h"
#include "Text_Format.h"

enum
{
    OLED_TYPE_Y = 0u,
    OLED_VALUE_Y = 16u,
    OLED_FREQUENCY_Y = 32u,
    OLED_RANGE_Y = 48u,
    OLED_WIDTH = 128u,
    OLED_ROW_HEIGHT = 8u,
    OLED_TEXT_CAPACITY = OLED_WIDTH / OLED_6X8 + 1u
};

bool OLED_DisplayInit(void)
{
    OLED_Init();
    return OLED_GetLastError() == OLED_ERROR_NONE;
}

/* 刷新一页前先清本行显存，以免短字符串残留上一帧尾部。 */
static bool RenderLine(uint8_t y, const char *line)
{
    OLED_ClearArea(0, y, OLED_WIDTH, OLED_ROW_HEIGHT);
    OLED_ShowString(0, y, line, OLED_6X8);
    OLED_UpdateArea(0, y, OLED_WIDTH, OLED_ROW_HEIGHT);
    return OLED_GetLastError() == OLED_ERROR_NONE;
}

bool OLED_ShowLines(const char *line1, const char *line2, const char *line3, const char *line4)
{
    return RenderLine(OLED_TYPE_Y, line1) && RenderLine(OLED_VALUE_Y, line2) &&
           RenderLine(OLED_FREQUENCY_Y, line3) && RenderLine(OLED_RANGE_Y, line4);
}

bool OLED_ShowActivity(const char *message)
{
    return RenderLine(OLED_TYPE_Y, message);
}

''' + display[display.index('const char *OLED_GetStatusText'):]
write('System/OLED_Display.c', display)
edit('System/OLED_Display.h', 'bool OLED_Init(void);', 'bool OLED_DisplayInit(void);')
edit('System/OLED_Display.h', '每行最多 21 个 ASCII 字符，超出部分不显示。', '每行可容纳 21 个完整的 ASCII 字符。')
edit('System/App_RLC.c', 'display_ready = OLED_Init();', 'display_ready = OLED_DisplayInit();')
edit('System/OLED_I2C.h', '通常由 OLED_Display.c 调用。', '由 OLED.c 调用。')

test = read('Tests/test_board_deadline.c')
test = replace(test, 'DDS 替身调用 SysTick_Handler() 推进真实板级计数', 'DDS 替身推进模拟的 GetTick() 计数')
test = replace(test, '#include "OLED_I2C.h"', '#include "OLED_I2C.h"\n#include "delay.h"')
test = replace(test, 'void SysTick_Handler(void);', '''static uint32_t ticks_ms;

void Delay_Init(void) { ticks_ms = 0u; }
void Delay(uint32_t ms) { ticks_ms += ms; }
uint32_t GetTick(void) { return ticks_ms; }''')
assert test.count('SysTick_Handler();') == 2
write('Tests/test_board_deadline.c', test.replace('SysTick_Handler();', '++ticks_ms;'))

# Register six reference files in all three Keil targets, retaining debugger options.
project = read('project.uvprojx').replace('RLC_V1_1_', 'RLC_V1_2_')
entries = ''
for name in names:
    kind = 1 if name.endswith('.c') else 5
    entries += f'''            <File>
              <FileName>{name}</FileName>
              <FileType>{kind}</FileType>
              <FilePath>.\\System\\{name}</FilePath>
            </File>
'''
project, count = re.subn(r'(<GroupName>System - Hardware</GroupName>\s*<Files>\n)(.*?)(          </Files>)',
                        lambda m: m[1] + m[2] + entries + m[3], project, flags=re.S)
assert count == 3
write('project.uvprojx', project)

opt = read('project.uvoptx')
new_entries = ''
for name in names:
    kind = 1 if name.endswith('.c') else 5
    new_entries += f'''    <File>
      <GroupNumber>4</GroupNumber>
      <FileNumber>0</FileNumber>
      <FileType>{kind}</FileType>
      <tvExp>0</tvExp>
      <tvExpOptDlg>0</tvExpOptDlg>
      <bDave2>0</bDave2>
      <PathWithFileName>.\\System\\{name}</PathWithFileName>
      <FilenameWithoutPath>{name}</FilenameWithoutPath>
      <RteFlg>0</RteFlg>
      <bShared>0</bShared>
    </File>
'''
opt, count = re.subn(r'(<GroupName>System - Hardware</GroupName>\n)(.*?)(  </Group>)',
                    lambda m: m[1] + m[2] + new_entries + m[3], opt, flags=re.S)
assert count == 1
numbers = iter(range(1, 69))
opt, count = re.subn(r'<FileNumber>\d+</FileNumber>', lambda m: f'<FileNumber>{next(numbers)}</FileNumber>', opt)
assert count == 68
write('project.uvoptx', opt)

for target in ET.fromstring(project).findall('./Targets/Target'):
    files = [n.text for n in target.findall('Groups/Group/Files/File/FilePath')]
    assert len(files) == len(set(files)) == 68
    assert files == [n.text for n in ET.fromstring(opt).findall('Group/File/PathWithFileName')]
    assert all((dest / p).is_file() for p in files)

build = read('build_firmware.ps1').replace('RLC_V1_1_', 'RLC_V1_2_').replace('RLC_V1.1_', 'RLC_V1.2_')
write('build_firmware.ps1', build)
edit('USER/main.c', 'RLC 电桥 V1.1', 'RLC 电桥 V1.2')
edit('System/BSP_PinMap.h', 'V1.1 候选', 'V1.2 候选')
print('V1.2 created from the current SPL project; 68 files per target.')
